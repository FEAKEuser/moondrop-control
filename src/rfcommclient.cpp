// SPDX-License-Identifier: GPL-3.0-or-later
#include "rfcommclient.h"

#include "i18n.h"

#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// AF_BLUETOOTH definitions.
//
// We deliberately avoid <bluetooth/bluetooth.h> (bluez-libs-devel) so that the
// plugin builds with nothing but Qt.  The ABI used here is stable and defined
// by the Linux kernel (include/net/bluetooth/rfcomm.h).
// ---------------------------------------------------------------------------
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_RFCOMM
#define BTPROTO_RFCOMM 3
#endif
#ifndef SOL_RFCOMM
#define SOL_RFCOMM 18
#endif

namespace {
// Layout matches the BlueZ <bluetooth/rfcomm.h> sockaddr_rc.  It is *not*
// packed: the trailing padding byte is part of the struct the kernel expects
// (connect() fails with EINVAL when the address length is 9 instead of 10).
struct SockaddrRc {
    sa_family_t rc_family;
    quint8 rc_bdaddr[6]; // little endian layout (last octet first), see BlueZ str2ba()
    quint8 rc_channel;
    quint8 rc_padding = 0;
};

// "AA:BB:CC:DD:EE:FF" -> bdaddr bytes in BlueZ order (reversed)
bool parseAddress(const QString &address, quint8 out[6])
{
    const QStringList parts = address.split(QLatin1Char(':'));
    if (parts.size() != 6) {
        return false;
    }
    quint8 bytes[6];
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        const int value = parts.at(i).toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xFF) {
            return false;
        }
        bytes[i] = quint8(value);
    }
    for (int i = 0; i < 6; ++i) {
        out[i] = bytes[5 - i];
    }
    return true;
}
} // namespace

namespace Moondrop {

RfcommClient::RfcommClient(QObject *parent)
    : Transport(parent)
{
    // The kernel retries RFCOMM connects for up to ~30 s on its own.  A channel
    // that does not answer is the normal case while the candidates are scanned,
    // so keep the deadline short and let the caller pick the next one.
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, &RfcommClient::onConnectTimeout);
}

RfcommClient::~RfcommClient()
{
    closeSocket();
}

void RfcommClient::connectToDevice(const QString &address, int channel, int timeoutMs)
{
    closeSocket();
    timeoutMs = qBound(700, timeoutMs, 8000);

    quint8 bdaddr[6];
    if (!parseAddress(address, bdaddr)) {
        fail(moondropTr("Invalid Bluetooth address: %1").arg(address));
        return;
    }
    m_address = address;
    m_channel = channel;

    const int fd = ::socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, BTPROTO_RFCOMM);
    if (fd < 0) {
        fail(moondropTr("Unable to open RFCOMM socket: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    SockaddrRc addr {};
    addr.rc_family = AF_BLUETOOTH;
    ::memcpy(addr.rc_bdaddr, bdaddr, sizeof(bdaddr));
    addr.rc_channel = quint8(channel);

    m_fd = fd;
    m_connected = false;

    m_lastErrno = 0;
    const int rc = ::connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (rc == 0) {
        // connected immediately (rare for RFCOMM, but possible)
        m_connected = true;
        m_readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_readNotifier, &QSocketNotifier::activated, this, &RfcommClient::onReadable);
        m_writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
        connect(m_writeNotifier, &QSocketNotifier::activated, this, &RfcommClient::onWritable);
        updateWriteNotifier();
        Q_EMIT connected();
        return;
    }

    if (rc < 0 && errno != EINPROGRESS && errno != EAGAIN) {
        const QString message = QString::fromLocal8Bit(strerror(errno));
        m_lastErrno = errno;
        closeSocket();
        fail(moondropTr("Cannot connect to %1 on RFCOMM channel %2: %3").arg(address).arg(channel).arg(message));
        return;
    }

    m_writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &RfcommClient::onWritable);
    m_writeNotifier->setEnabled(true);
    m_connectTimer->start(timeoutMs);
}

void RfcommClient::disconnectFromDevice()
{
    const bool wasConnected = isConnected();
    closeSocket();
    if (wasConnected) {
        Q_EMIT disconnected();
    }
}

void RfcommClient::closeSocket()
{
    m_connectTimer->stop();
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->disconnect();
        m_readNotifier->deleteLater();
        m_readNotifier = nullptr;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
        m_writeNotifier->disconnect();
        m_writeNotifier->deleteLater();
        m_writeNotifier = nullptr;
    }
    if (m_fd >= 0) {
        // A connect() that is still in progress keeps the pending RFCOMM request
        // alive in the kernel after close(), which makes the next connect() to
        // the same device fail with EBUSY until that request finally times out
        // (measured: ~1 s per abandoned attempt).  Ask the kernel to drop it.
        ::shutdown(m_fd, SHUT_RDWR);
        ::close(m_fd);
        m_fd = -1;
    }
    m_connected = false;
    m_writeBuffer.clear();
}

void RfcommClient::fail(const QString &message)
{
    // Errors are always reported; while probing channels most of them are
    // expected and the device model decides whether to surface them.
    Q_EMIT errorOccurred(message);
}

void RfcommClient::updateWriteNotifier()
{
    if (!m_writeNotifier || m_fd < 0) {
        return;
    }
    m_writeNotifier->setEnabled(!m_writeBuffer.isEmpty() || !m_connected);
}

qint64 RfcommClient::write(const QByteArray &data)
{
    if (m_fd < 0) {
        return -1;
    }
    m_writeBuffer.append(data);
    updateWriteNotifier();
    if (m_connected) {
        onWritable();
    }
    return data.size();
}

void RfcommClient::onWritable()
{
    if (m_fd < 0) {
        return;
    }
    if (!m_connected) {
        int error = 0;
        socklen_t len = sizeof(error);
        if (::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            fail(moondropTr("Connection check failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            disconnectFromDevice();
            return;
        }
        if (error != 0) {
            const QString message = QString::fromLocal8Bit(strerror(error));
            m_lastErrno = error;
            closeSocket();
            fail(moondropTr("Cannot connect to %1 on RFCOMM channel %2: %3").arg(m_address).arg(m_channel).arg(message));
            return;
        }
        m_connected = true;
        m_connectTimer->stop();
        m_readNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_readNotifier, &QSocketNotifier::activated, this, &RfcommClient::onReadable);
        Q_EMIT connected();
    }

    while (!m_writeBuffer.isEmpty()) {
        const qint64 written = ::send(m_fd, m_writeBuffer.constData(), size_t(m_writeBuffer.size()), MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break;
            }
            const QString message = QString::fromLocal8Bit(strerror(errno));
            m_lastErrno = errno;
            closeSocket();
            fail(moondropTr("Write error: %1").arg(message));
            Q_EMIT disconnected();
            return;
        }
        m_writeBuffer.remove(0, int(written));
    }
    updateWriteNotifier();
}

void RfcommClient::onReadable()
{
    if (m_fd < 0) {
        return;
    }
    char buffer[4096];
    while (true) {
        const qint64 n = ::recv(m_fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            Q_EMIT dataReceived(QByteArray(buffer, int(n)));
            continue;
        }
        if (n == 0) {
            // peer closed the connection
            closeSocket();
            Q_EMIT disconnected();
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        const QString message = QString::fromLocal8Bit(strerror(errno));
        m_lastErrno = errno;
        closeSocket();
        fail(moondropTr("Read error: %1").arg(message));
        Q_EMIT disconnected();
        return;
    }
}

void RfcommClient::onConnectTimeout()
{
    if (m_connected || m_fd < 0) {
        return;
    }
    const QString message = moondropTr("Timeout connecting to %1 on RFCOMM channel %2").arg(m_address).arg(m_channel);
    closeSocket();
    fail(message);
}

} // namespace Moondrop
