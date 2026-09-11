// SPDX-License-Identifier: GPL-3.0-or-later
#include "gaia.h"

namespace Moondrop {

QString featureName(quint16 feature)
{
    switch (feature) {
    case FeatureBasic: return QStringLiteral("BASIC");
    case FeatureEarbud: return QStringLiteral("EARBUD");
    case FeatureAnc: return QStringLiteral("ANC");
    case FeatureVoiceUi: return QStringLiteral("VOICE_UI");
    case FeatureDebug: return QStringLiteral("DEBUG");
    case FeatureMusicProcessing: return QStringLiteral("EQ");
    case FeatureUpgrade: return QStringLiteral("UPGRADE");
    case FeatureHandsetService: return QStringLiteral("HANDSET");
    case FeatureAudioCuration: return QStringLiteral("AUDIO_CURATION");
    case FeatureEarbudFit: return QStringLiteral("EARBUD_FIT");
    case FeatureVoiceProcessing: return QStringLiteral("VOICE_PROCESSING");
    case FeatureGesture: return QStringLiteral("GESTURE");
    case FeatureStatistics: return QStringLiteral("STATISTICS");
    case FeatureBattery: return QStringLiteral("BATTERY");
    case FeatureVoice: return QStringLiteral("VOICE");
    case FeatureDacGain: return QStringLiteral("DAC_GAIN");
    case FeatureCodecType: return QStringLiteral("CODEC");
    case FeatureLightSensor: return QStringLiteral("LIGHT_SENSOR");
    case FeatureSpatialAudio: return QStringLiteral("SPATIAL_AUDIO");
    case FeatureLed: return QStringLiteral("LED");
    case FeatureOneBringTwo: return QStringLiteral("MULTIPOINT");
    case FeatureBtAddress: return QStringLiteral("BT_ADDRESS");
    case FeatureTouchV2: return QStringLiteral("TOUCH_V2");
    case FeatureAudioResource: return QStringLiteral("AUDIO_RESOURCE");
    case FeaturePowerControl: return QStringLiteral("POWER");
    case FeaturePowerTimeout: return QStringLiteral("POWER_TIMEOUT");
    case FeatureTouchV3: return QStringLiteral("TOUCH_V3");
    case FeatureDyBass: return QStringLiteral("DYBASS");
    case FeatureAudioFileStorage: return QStringLiteral("AUDIO_FILE");
    case FeatureLrChannel: return QStringLiteral("LR_CHANNEL");
    case FeatureAncV2: return QStringLiteral("ANC_V2");
    default: return QStringLiteral("F%1").arg(feature);
    }
}

QString Frame::toString() const
{
    return QStringLiteral("F%1(%2) type=%3 cmd=%4 len=%5 %6")
        .arg(feature())
        .arg(featureName(feature()))
        .arg(type())
        .arg(cmd())
        .arg(payload.size())
        .arg(QString::fromLatin1(payload.toHex(' ')));
}

QByteArray encodeFrame(quint16 feature, quint8 command, const QByteArray &payload, quint8 type, quint16 vendor)
{
    const quint16 cmd = commandValue(feature, command, type);
    QByteArray out;
    out.reserve(8 + payload.size());
    out.append(char(FrameStart));
    out.append(char(ProtocolVersion));
    out.append(char((payload.size() >> 8) & 0xFF));
    out.append(char(payload.size() & 0xFF));
    out.append(char((vendor >> 8) & 0xFF));
    out.append(char(vendor & 0xFF));
    out.append(char((cmd >> 8) & 0xFF));
    out.append(char(cmd & 0xFF));
    out.append(payload);
    return out;
}

QList<Frame> Stream::feed(const QByteArray &data)
{
    m_buffer.append(data);
    QList<Frame> frames;
    int i = 0;
    while (true) {
        // resynchronise on the start byte
        while (i < m_buffer.size() && quint8(m_buffer.at(i)) != FrameStart) {
            ++i;
        }
        if (m_buffer.size() - i < 8) {
            break;
        }
        const quint16 length = (quint8(m_buffer.at(i + 2)) << 8) | quint8(m_buffer.at(i + 3));
        const int total = 8 + length;
        if (m_buffer.size() - i < total) {
            break;
        }
        const char *p = m_buffer.constData() + i;
        Frame frame;
        frame.version = quint8(p[1]);
        frame.payloadLength = length;
        frame.vendor = (quint8(p[4]) << 8) | quint8(p[5]);
        frame.command = (quint8(p[6]) << 8) | quint8(p[7]);
        frame.payload = QByteArray(p + 8, length);
        frames.append(frame);
        i += total;
    }
    m_buffer.remove(0, i);
    return frames;
}

} // namespace Moondrop
