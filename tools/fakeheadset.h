// SPDX-License-Identifier: GPL-3.0-or-later
// A fake MOONDROP headphone for UI development.
//
// Implements the Transport interface with canned answers modelled on a real
// MOONDROP EDGE (firmware 1.4.0); the Pudding and Robin variants follow the
// published captures.  This keeps the UI and the state machine testable without
// Bluetooth hardware - and without taking the single RFCOMM link away from a
// running Plasma widget.
#pragma once

#include "gaia.h"
#include "transport.h"

#include <QObject>
#include <QTimer>

#include <cerrno>

#include <cstdio>

namespace Moondrop {

// A fake MOONDROP headphone, as a Transport.
//
// It answers the commands the way a real device does (measured on an EDGE with
// firmware 1.4.0; the Pudding and Robin frames follow the published captures), so
// pages, curves and error paths can be developed and looked at without hardware -
// and without occupying the single RFCOMM link a real headphone offers.
class FakeHeadset : public Transport
{
    Q_OBJECT
public:
    enum Model { Edge, Pudding, Robin };

    explicit FakeHeadset(Model model = Edge, QObject *parent = nullptr)
        : Transport(parent)
        , m_model(model)
    {
    }

    bool isConnected() const override { return m_connected; }
    QString address() const override { return QStringLiteral("00:11:22:33:44:55"); }
    int channel() const override { return m_channel; }
    int lastErrno() const override { return m_lastErrno; }

    void connectToDevice(const QString &address, int channel, int timeoutMs = 8000) override
    {
        Q_UNUSED(address)
        Q_UNUSED(timeoutMs)
        if (qEnvironmentVariableIsSet("MOONDROP_DEBUG")) {
            std::fprintf(stderr, "[fake] connectToDevice channel=%d (expected=%d connected=%d silent=%d)\n", channel, m_expectedChannel, int(m_connected), int(m_silent));
        }
        m_channel = channel;
        if (m_busy) {
            // the kernel refuses immediately when the slot is taken
            QTimer::singleShot(20, this, [this] {
                m_lastErrno = EBUSY;
                Q_EMIT errorOccurred(QStringLiteral("Cannot connect: device or resource busy"));
            });
            return;
        }
        // a real RFCOMM connect takes a moment
        QTimer::singleShot(m_channel == m_expectedChannel ? 40 : 120, this, [this] {
            m_connected = true;
            // A wrong channel accepts the connection but is not the GAIA
            // channel, so nothing ever answers on it.  The scanner has to notice
            // the silence and move on - modelling this as "answers anyway" would
            // hide exactly the bug that made connecting feel slow.  Set the flag
            // *before* the signal: the scanner writes its probe the moment it
            // hears about the connection.  The flag is deliberately not cleared
            // by disconnectFromDevice(): one socket is reused for the whole scan,
            // so the next channel has to set its own state anyway.
            m_silent = (m_channel != m_expectedChannel);
            Q_EMIT connected();
        });
    }

    void disconnectFromDevice() override
    {
        if (qEnvironmentVariableIsSet("MOONDROP_DEBUG")) {
            std::fprintf(stderr, "[fake] disconnectFromDevice (connected=%d)\n", int(m_connected));
        }
        if (!m_connected) {
            return;
        }
        m_connected = false;
        Q_EMIT disconnected();
    }

    qint64 write(const QByteArray &data) override
    {
        if (!m_connected || m_silent) {
            return m_connected ? data.size() : -1;
        }
        Stream stream;
        const QList<Frame> frames = stream.feed(data);
        for (const Frame &sent : frames) {
            // the firmware needs a moment before it answers
            QTimer::singleShot(15, this, [this, sent] { respond(sent); });
        }
        return data.size();
    }

    // the channel the fake device listens on
    void setChannel(int channel) { m_expectedChannel = channel; }

    // Pretend a different headphone is now connected (used by the switch check:
    // one widget, the user swaps headphones).
    void setModel(Model model) { m_model = model; }

    // Model "somebody else holds the single control connection": every connect
    // attempt fails with EBUSY, like a real headphone whose slot is taken.
    void setBusy(bool busy) { m_busy = busy; }

    // the UI mode (0 = off, 1 = anc, 2 = transparency, 3 = wind) stays the same,
    // only the wire encoding differs between writing and reading
    static int writeValueToUiMode(int value)
    {
        switch (value) {
        case 1: return 0; // off
        case 2: return 1; // anc
        case 4: return 2; // transparency
        case 3: return 3; // wind
        default: return -1;
        }
    }

    static int uiModeToReadValue(int mode)
    {
        return mode >= 0 && mode <= 3 ? mode : 0;
    }

private:
    void respond(const Frame &sent)
    {
        const quint16 feature = sent.feature();
        const quint8 command = sent.cmd();

        if (feature == FeatureBasic) {
            switch (command) {
            case 0:
                reply(feature, 0, QByteArray::fromHex("0301"));
                return;
            case 1:
                if (m_model == Pudding) {
                    reply(feature, 1, QByteArray::fromHex("00010120010d011001"));
                } else if (m_model == Robin) {
                    reply(feature, 1, QByteArray::fromHex("000108010d01"));
                } else {
                    reply(feature, 1, QByteArray::fromHex("000301050101010602000208010f0110011101140115010d01"));
                }
                return;
            case 3:
                reply(feature, 3, QByteArray("ABCDEF0123456789"));
                return;
            case 4:
                reply(feature, 4, m_model == Pudding ? QByteArray("MOONDROP Pudding")
                                        : m_model == Robin ? QByteArray("MOONDROP Robin")
                                                           : QByteArray("MOONDROP EDGE"));
                return;
            case 5:
                reply(feature, 5, QByteArray("1.4.0"));
                return;
            case 22:
                if (m_model == Edge) {
                    reply(feature, 22, QByteArray(1, '\x01'));
                }
                return;
            default:
                return;
            }
        }

        if (feature == FeatureBtAddress) {
            reply(feature, command, QByteArray::fromHex("d41761b292d4"));
            return;
        }

        if (feature == FeatureBattery) {
            if (command == 0) {
                reply(feature, 0, QByteArray(1, '\x00'));
            } else if (command == 1) {
                if (m_model == Pudding) {
                    reply(feature, 1, QByteArray::fromHex("015b024c0332"));
                } else if (m_model == Robin) {
                    reply(feature, 1, QByteArray::fromHex("015b024c"));
                } else {
                    reply(feature, 1, QByteArray::fromHex("0064"));
                }
            }
            return;
        }

        if (feature == FeatureMusicProcessing && m_model == Edge) {
            switch (command) {
            case 0:
                reply(feature, 0, QByteArray(1, '\x01'));
                return;
            case 1:
                reply(feature, 1, QByteArray::fromHex("0600010203043f"));
                return;
            case 2:
                reply(feature, 2, QByteArray(1, char(quint8(m_preset))));
                return;
            case 3:
                if (!sent.payload.isEmpty()) {
                    m_preset = quint8(sent.payload.at(0));
                    reply(feature, 3, QByteArray(1, char(quint8(m_preset))));
                }
                return;
            case 4:
                reply(feature, 4, QByteArray(1, '\x05'));
                return;
            case 5:
                deliver(encodeFrame(feature, 5, m_eq, TypeResponse));
                // the device announces EQ state changes as notifications
                QTimer::singleShot(5, this, [this] {
                    deliver(encodeFrame(FeatureMusicProcessing, 0, QByteArray(1, '\x01'), TypeNotification));
                });
                return;
            case 6:
                if (sent.payload.size() == m_eq.size()) {
                    m_eq = sent.payload;
                }
                return;
            default:
                return;
            }
        }

        if (feature == FeatureAudioCuration) {
            if (command == 3) {
                QByteArray payload(4, '\x00');
                payload[0] = char(quint8(uiModeToReadValue(m_ancMode)));
                payload[1] = '\x01';
                reply(feature, 3, payload);
            } else if (command == 4 && !sent.payload.isEmpty()) {
                const int mode = writeValueToUiMode(quint8(sent.payload.at(0)));
                if (mode >= 0) {
                    m_ancMode = mode;
                }
            }
            return;
        }

        if (feature == FeatureAncV2) {
            // ANC V2 uses the same value for reading and writing
            if (command == 3) {
                reply(feature, 3, QByteArray(1, char(quint8(m_ancMode))));
            } else if (command == 4 && !sent.payload.isEmpty() && quint8(sent.payload.at(0)) <= 3) {
                m_ancMode = quint8(sent.payload.at(0));
            }
            return;
        }

        if (feature == FeatureCodecType && command == 2) {
            reply(feature, 2, QByteArray(1, m_ldac ? '\x01' : '\x00'));
            return;
        }

        if (feature == FeatureDacGain && command == 1 && m_model == Edge) {
            reply(feature, 1, QByteArray(1, char(quint8(m_gain))));
            return;
        }
    }

    void reply(quint16 feature, quint8 command, const QByteArray &payload)
    {
        deliver(encodeFrame(feature, command, payload, TypeResponse));
    }

    void deliver(const QByteArray &bytes)
    {
        // like a real link: nothing arrives while the connection is down
        if (!m_connected) {
            return;
        }
        Q_EMIT dataReceived(bytes);
    }

    Model m_model;
    bool m_busy = false;
    bool m_connected = false;
    // connected to a channel that is not the GAIA one: accepts and stays silent
    bool m_silent = false;
    int m_channel = 0;
    int m_expectedChannel = 1;
    int m_lastErrno = 0;
    int m_ancMode = 0;
    int m_preset = 0x3F;
    int m_gain = 0;
    bool m_ldac = false;
    QByteArray m_eq = QByteArray::fromHex(
        "000400000017066600ffa600f051990000a2057864cc0000b408fc1ccc00ff521af413330000b4");
};

} // namespace Moondrop
