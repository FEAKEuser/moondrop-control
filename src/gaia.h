// SPDX-License-Identifier: GPL-3.0-or-later
// GAIA v3/v4 frame codec used by MOONDROP headphones over Bluetooth RFCOMM/SPP.
//
// Wire format (both directions):
//
//   FF | 04 | len_hi len_lo | vendor_hi vendor_lo | cmd_hi cmd_lo | payload...
//    0    1        2   3            4         5          6     7          8..
//
//   * byte 1          : protocol version (0x04 on MOONDROP EDGE)
//   * bytes 2-3       : payload length, big endian (payload only, no header)
//   * bytes 4-5       : vendor id, big endian (0x001D = MOONDROP/QTiL)
//   * bytes 6-7       : command value, big endian
//                       cmd = (feature << 9) | (type << 7) | command
//                       type: 0 = command, 1 = notification, 2 = response, 3 = error
//
// Example (firmware version query):
//   -> ff 04 00 00 00 1d 01 05
//   <- ff 04 00 05 00 1d 01 05  "1.4.0"
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace Moondrop {

constexpr quint16 VendorMoondrop = 0x001D;
constexpr quint16 VendorGaia = 0x000A;
constexpr quint8 ProtocolVersion = 0x04;
constexpr quint8 FrameStart = 0xFF;

enum PacketType : quint8 {
    TypeCommand = 0,
    TypeNotification = 1,
    TypeResponse = 2,
    TypeError = 3,
};

// QTiL feature ids (as used by MOONDROP firmware / official app).
enum Feature : quint16 {
    FeatureBasic = 0,
    FeatureEarbud = 1,
    FeatureAnc = 2,
    FeatureVoiceUi = 3,
    FeatureDebug = 4,
    FeatureMusicProcessing = 5, // EQ / PEQ
    FeatureUpgrade = 6,
    FeatureHandsetService = 7,
    FeatureAudioCuration = 8, // ANC control path on EDGE / GA2
    FeatureEarbudFit = 9,
    FeatureVoiceProcessing = 10,
    FeatureGesture = 11,
    FeatureStatistics = 12,
    FeatureBattery = 13,
    FeatureVoice = 14,
    FeatureDacGain = 15,
    FeatureCodecType = 16,
    FeatureLightSensor = 17,
    FeatureSpatialAudio = 18,
    FeatureLed = 19,
    FeatureOneBringTwo = 20,
    FeatureBtAddress = 21,
    FeatureTouchV2 = 22,
    FeatureAudioResource = 23,
    FeaturePowerControl = 24,
    FeaturePowerTimeout = 25,
    FeatureTouchV3 = 26,
    FeatureDyBass = 27,
    FeatureAudioFileStorage = 29,
    FeatureLrChannel = 30,
    FeatureAncV2 = 32,
};

QString featureName(quint16 feature);

struct Frame
{
    quint8 version = ProtocolVersion;
    quint16 payloadLength = 0;
    quint16 vendor = VendorMoondrop;
    quint16 command = 0;
    QByteArray payload;

    quint16 feature() const { return (command >> 9) & 0x7FF; }
    quint8 type() const { return (command >> 7) & 0x03; }
    quint8 cmd() const { return command & 0x7F; }

    QString toString() const;
};

inline quint16 commandValue(quint16 feature, quint8 command, quint8 type = TypeCommand)
{
    return quint16((feature << 9) | ((type & 0x03) << 7) | (command & 0x7F));
}

QByteArray encodeFrame(quint16 feature,
                       quint8 command,
                       const QByteArray &payload = QByteArray(),
                       quint8 type = TypeCommand,
                       quint16 vendor = VendorMoondrop);

// Incremental parser for the byte stream coming from the RFCOMM socket.
class Stream
{
public:
    QList<Frame> feed(const QByteArray &data);
    void clear() { m_buffer.clear(); }
    QByteArray rawBuffer() const { return m_buffer; }

private:
    QByteArray m_buffer;
};

} // namespace Moondrop
