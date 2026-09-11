// SPDX-License-Identifier: GPL-3.0-or-later
// The byte channel to the headphone.
//
// MoondropDevice only ever talks to this interface, which keeps the protocol and
// state logic independent of the transport.  The shipped implementation is
// RfcommClient (Bluetooth RFCOMM/SPP); the development tools plug in a fake
// implementation so the UI and the state machine can be exercised without
// hardware.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace Moondrop {

class Transport : public QObject
{
    Q_OBJECT
public:
    explicit Transport(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~Transport() override = default;

    virtual bool isConnected() const = 0;
    virtual QString address() const = 0;
    virtual int channel() const = 0;
    // errno of the last failure, 0 when there was none; lets the caller react on
    // EBUSY without depending on the translated error text
    virtual int lastErrno() const = 0;

    virtual void connectToDevice(const QString &address, int channel, int timeoutMs = 8000) = 0;
    virtual void disconnectFromDevice() = 0;
    virtual qint64 write(const QByteArray &data) = 0;

Q_SIGNALS:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void dataReceived(const QByteArray &data);
};

} // namespace Moondrop
