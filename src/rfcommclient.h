// SPDX-License-Identifier: MIT
// Minimal asynchronous Bluetooth RFCOMM (SPP) client built directly on the
// Linux AF_BLUETOOTH socket API.  No BlueZ D-Bus profile registration and no
// libbluetooth dependency are needed - a plain RFCOMM client socket works for
// unprivileged users on a BlueZ based system (the device must be paired).
#pragma once

#include "transport.h"

#include <QObject>
#include <QString>

class QSocketNotifier;
class QTimer;

namespace Moondrop {

class RfcommClient : public Transport
{
    Q_OBJECT
public:
    explicit RfcommClient(QObject *parent = nullptr);
    ~RfcommClient() override;

    bool isConnected() const override { return m_fd >= 0 && m_connected; }
    bool isConnecting() const { return m_fd >= 0 && !m_connected; }

    void connectToDevice(const QString &address, int channel, int timeoutMs = 8000) override;
    void disconnectFromDevice() override;

    qint64 write(const QByteArray &data) override;

    QString address() const override { return m_address; }
    int channel() const override { return m_channel; }
    // errno of the last failure (0 when there was none); used to react on EBUSY
    // without depending on the translated error text
    int lastErrno() const override { return m_lastErrno; }

private Q_SLOTS:
    void onReadable();
    void onWritable();
    void onConnectTimeout();

private:
    void closeSocket();
    void fail(const QString &message);
    void updateWriteNotifier();

    int m_fd = -1;
    int m_lastErrno = 0;
    bool m_connected = false;
    QString m_address;
    int m_channel = 0;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer *m_connectTimer = nullptr;
    QByteArray m_writeBuffer;
};

} // namespace Moondrop
