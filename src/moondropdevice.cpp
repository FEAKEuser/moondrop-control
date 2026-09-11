// SPDX-License-Identifier: MIT
#include "moondropdevice.h"

#include "devicediscovery.h"
#include "i18n.h"
#include "rfcommclient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

#include <cerrno>
#include <QDebug>
#include <QTimer>

namespace Moondrop {

namespace {

constexpr quint8 CBasicGetGaiaVersion = 0;
constexpr quint8 CBasicGetSupportedFeatures = 1;
constexpr quint8 CBasicGetSerialNumber = 3;
constexpr quint8 CBasicGetVariant = 4;
constexpr quint8 CBasicGetApplicationVersion = 5;
constexpr quint8 CBasicGetTwsStatus = 22;

constexpr quint8 CBatteryGetSupported = 0;
constexpr quint8 CBatteryGetLevels = 1;

constexpr quint8 CAncGetState = 1;
constexpr quint8 CAncGetCurrentMode = 4;

constexpr quint8 CAncV2GetCurrentMode = 3;
constexpr quint8 CAncV2SetCurrentMode = 4;

constexpr quint8 CAcGetCurrentMode = 3;
constexpr quint8 CAcSetMode = 4;

constexpr quint8 CEqGetState = 0;
constexpr quint8 CEqGetPresets = 1;
constexpr quint8 CEqGetSet = 2;
constexpr quint8 CEqSelectSet = 3;
constexpr quint8 CEqGetBandCount = 4;
constexpr quint8 CEqGetUserConfig = 5;
constexpr quint8 CEqSetUserConfig = 6;

constexpr quint8 CCodecGetLc3 = 1;
constexpr quint8 CCodecGetLdac = 2;
constexpr quint8 CCodecSetLc3 = 3;
constexpr quint8 CCodecSetLdac = 4;
constexpr quint8 CCodecGetLhdc = 5;
constexpr quint8 CCodecSetLhdc = 6;

constexpr quint8 CDacGetGain = 1;
constexpr quint8 CDacSetGain = 2;

constexpr quint8 CMultipointGet = 1;
constexpr quint8 CMultipointSet = 2;

constexpr quint8 CBtAddressGet = 1;

constexpr int UserPresetId = 0x3F;

QString macFromBytes(const QByteArray &payload)
{
    if (payload.size() < 6) {
        return QString();
    }
    QStringList parts;
    for (int i = 0; i < 6; ++i) {
        parts << QStringLiteral("%1").arg(quint8(payload.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(QLatin1Char(':'));
}

int signed16(quint8 hi, quint8 lo)
{
    const qint16 value = qint16((quint16(hi) << 8) | quint16(lo));
    return value;
}

} // namespace

// ---------------------------------------------------------------------------
// 5 band user PEQ payload (39 bytes on EDGE style firmware)
// ---------------------------------------------------------------------------
QList<EqBand> decodeUserEq(const QByteArray &payload)
{
    QList<EqBand> bands;
    // 4 byte header + 5 * 7 byte band records
    if (payload.size() < 4 + 5 * 7) {
        return bands;
    }
    for (int i = 0; i < 5; ++i) {
        const int off = 4 + (i * 7);
        EqBand band;
        band.frequency = (quint8(payload.at(off)) << 8) | quint8(payload.at(off + 1));
        const quint16 qRaw = (quint8(payload.at(off + 2)) << 8) | quint8(payload.at(off + 3));
        band.q = double(qRaw) / 4096.0;
        band.type = quint8(payload.at(off + 4));
        band.gain = double(signed16(quint8(payload.at(off + 5)), quint8(payload.at(off + 6)))) / 60.0;
        bands.append(band);
    }
    return bands;
}

QByteArray encodeUserEq(const QList<EqBand> &bands)
{
    if (bands.size() != 5) {
        return QByteArray();
    }
    QByteArray out;
    out.reserve(39);
    out.append(char(0x00));
    out.append(char(0x04)); // start band / end band header, as emitted by the firmware
    out.append(char(0x00));
    out.append(char(0x00));
    for (const EqBand &band : bands) {
        const quint16 freq = quint16(qBound(20, band.frequency, 20000));
        const quint16 qRaw = quint16(qBound(0, int(qRound(band.q * 4096.0)), 0xFFFF));
        const qint16 gainRaw = qint16(qBound(-20000, int(qRound(band.gain * 60.0)), 20000));
        out.append(char((freq >> 8) & 0xFF));
        out.append(char(freq & 0xFF));
        out.append(char((qRaw >> 8) & 0xFF));
        out.append(char(qRaw & 0xFF));
        out.append(char(band.type & 0xFF));
        out.append(char((quint16(gainRaw) >> 8) & 0xFF));
        out.append(char(quint16(gainRaw) & 0xFF));
    }
    return out;
}

// ---------------------------------------------------------------------------

MoondropDevice::MoondropDevice(QObject *parent, Transport *transport)
    : QObject(parent)
{
    m_transport = transport ? transport : new RfcommClient(nullptr);
    m_transport->setParent(this);
    // MOONDROP_CONFIG points the settings at a specific file, which the tests and
    // the development tools use so they never touch the user's configuration.
    const QString configPath = qEnvironmentVariable("MOONDROP_CONFIG");
    if (!configPath.isEmpty()) {
        m_settings = new QSettings(configPath, QSettings::IniFormat, this);
    } else {
        m_settings = new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                   QStringLiteral("moondrop-widget"), QStringLiteral("config"), this);
    }

    connect(m_transport, &Transport::connected, this, &MoondropDevice::onTransportConnected);
    connect(m_transport, &Transport::disconnected, this, &MoondropDevice::onTransportDisconnected);
    connect(m_transport, &Transport::dataReceived, this, &MoondropDevice::onDataReceived);
    connect(m_transport, &Transport::errorOccurred, this, &MoondropDevice::onError);

    m_requestTimer = new QTimer(this);
    m_requestTimer->setInterval(60);
    connect(m_requestTimer, &QTimer::timeout, this, &MoondropDevice::onRequestTick);
    m_requestTimer->start();

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MoondropDevice::onReconnectTimer);

    m_probeTimer = new QTimer(this);
    m_probeTimer->setSingleShot(true);
    m_probeTimer->setInterval(1600);
    connect(m_probeTimer, &QTimer::timeout, this, [this] {
        if (m_state == Connecting) {
            // the channel did not answer at all: try the next candidate
            advanceChannel();
        }
    });

    loadSettings();

    // restore the saved device curve (kept so an accidental change can be undone)
    const QString backup = m_settings->value(QStringLiteral("eq/deviceBackup")).toString();
    if (!backup.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(backup.toUtf8());
        QVariantList list;
        for (const QJsonValue &value : document.array()) {
            const QJsonObject object = value.toObject();
            QVariantMap band;
            band.insert(QStringLiteral("frequency"), object.value(QStringLiteral("frequency")).toInt());
            band.insert(QStringLiteral("gain"), object.value(QStringLiteral("gain")).toDouble());
            band.insert(QStringLiteral("q"), object.value(QStringLiteral("q")).toDouble());
            band.insert(QStringLiteral("type"), object.value(QStringLiteral("type")).toInt());
            list.append(band);
        }
        if (list.size() == 5) {
            m_deviceEqBackup = list;
        }
    }

    // Watch the Bluetooth level too: this is how the widget notices that the
    // headphones were switched on after Plasma started.
    m_bluetoothWatcher = new BlueZWatcher(this);
    connect(m_bluetoothWatcher, &BlueZWatcher::deviceConnected,
            this, &MoondropDevice::onDeviceAppeared);
    connect(m_bluetoothWatcher, &BlueZWatcher::deviceDisconnected,
            this, &MoondropDevice::onDeviceVanished);
    m_bluetoothWatcher->watch(m_settingsAddress);

    // Connect without the user having to press anything.  Delayed a little so the
    // applet is fully built and the system bus has settled.
    QTimer::singleShot(600, this, &MoondropDevice::onStartupAutoConnect);
}

void MoondropDevice::disableStartupAutoConnect()
{
    m_startupAutoConnect = false;
}

void MoondropDevice::onStartupAutoConnect()
{
    if (!m_startupAutoConnect || !m_autoConnect || m_settingsAddress.isEmpty()) {
        return;
    }
    if (m_state != Disconnected) {
        return; // somebody (a tool, or the user) was faster
    }
    Q_EMIT logMessage(moondropTr("Connecting to the headphone…"));
    connectDevice();
}

void MoondropDevice::onDeviceAppeared(const QString &address)
{
    Q_UNUSED(address)
    setWaitingForHeadphone(false);
    if (!m_autoConnect || m_state != Disconnected) {
        return;
    }
    Q_EMIT logMessage(moondropTr("The headphone connected, linking the control channel…"));
    // the A2DP link is up, so our channel should be free
    m_reconnectDelayMs = 2000;
    m_reconnectAttempts = 0;
    connectDevice();
}

void MoondropDevice::onDeviceVanished(const QString &address)
{
    Q_UNUSED(address)
    if (m_autoConnect) {
        setWaitingForHeadphone(true);
    }
    if (m_state == Disconnected) {
        return;
    }
    Q_EMIT logMessage(moondropTr("The headphone disconnected."));
    m_reconnectTimer->stop();
    m_probeTimer->stop();
    const bool wasBusy = busy();
    m_queue.clear();
    m_hasInFlight = false;
    if (wasBusy) {
        Q_EMIT busyChanged();
    }
    m_transport->disconnectFromDevice();
    setState(Disconnected);
}

MoondropDevice::~MoondropDevice()
{
    saveSettings();
}

QString MoondropDevice::statusText() const
{
    switch (m_state) {
    case Disconnected:
        return moondropTr("Disconnected");
    case Connecting:
        return moondropTr("Connecting…");
    case Connected:
        return moondropTr("Connected");
    case NotResponding:
        return moondropTr("Not responding");
    }
    return QString();
}

void MoondropDevice::loadSettings()
{
    m_settingsAddress = m_settings->value(QStringLiteral("device/address")).toString();
    m_settingsChannel = m_settings->value(QStringLiteral("device/channel"), 0).toInt();
    m_autoConnect = m_settings->value(QStringLiteral("device/autoConnect"), true).toBool();
    m_autoReconnect = m_settings->value(QStringLiteral("device/autoReconnect"), true).toBool();
    updatePresetNames();
}

void MoondropDevice::saveSettings()
{
    m_settings->setValue(QStringLiteral("device/address"), m_settingsAddress);
    m_settings->setValue(QStringLiteral("device/channel"), m_settingsChannel);
    m_settings->setValue(QStringLiteral("device/autoConnect"), m_autoConnect);
    m_settings->setValue(QStringLiteral("device/autoReconnect"), m_autoReconnect);
    m_settings->sync();
}

void MoondropDevice::setAddress(const QString &address)
{
    if (m_settingsAddress == address) {
        return;
    }
    m_settingsAddress = address;
    if (m_bluetoothWatcher) {
        m_bluetoothWatcher->watch(address);
    }
    saveSettings();
    Q_EMIT settingsChanged();
}

void MoondropDevice::setChannel(int channel)
{
    if (m_settingsChannel == channel) {
        return;
    }
    m_settingsChannel = channel;
    saveSettings();
    Q_EMIT settingsChanged();
}

void MoondropDevice::setAutoConnect(bool enabled)
{
    if (m_autoConnect == enabled) {
        return;
    }
    m_autoConnect = enabled;
    saveSettings();
    Q_EMIT settingsChanged();
}

void MoondropDevice::setAutoReconnect(bool enabled)
{
    if (m_autoReconnect == enabled) {
        return;
    }
    m_autoReconnect = enabled;
    saveSettings();
    Q_EMIT settingsChanged();
}

void MoondropDevice::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    if (state == Disconnected || state == NotResponding) {
        m_queue.clear();
        m_hasInFlight = false;
        if (m_ready) {
            m_ready = false;
            Q_EMIT readyChanged();
        }
    }
    Q_EMIT stateChanged();
}

void MoondropDevice::setError(const QString &message)
{
    m_lastError = message;
    Q_EMIT lastErrorChanged();
    Q_EMIT logMessage(message);
}

void MoondropDevice::clearError()
{
    if (m_lastError.isEmpty()) {
        return;
    }
    m_lastError.clear();
    Q_EMIT lastErrorChanged();
}

void MoondropDevice::connectDevice()
{
    if (m_settingsAddress.isEmpty()) {
        setError(moondropTr("No device selected. Pick your headphone in the widget settings."));
        return;
    }
    connectTo(m_settingsAddress, m_settingsChannel);
}

void MoondropDevice::connectTo(const QString &address, int channel)
{
    if (address.isEmpty()) {
        setError(moondropTr("No device address given."));
        return;
    }

    // Explicitly asked to connect: clear anything still pending so a stuck
    // request can never block this, and so `busy` is truthful for the UI.
    const bool wasBusy = busy();
    m_queue.clear();
    m_hasInFlight = false;
    m_probeTimer->stop();
    m_reconnectTimer->stop();
    if (wasBusy) {
        Q_EMIT busyChanged();
    }

    // Already connected to this device: nothing to do.  This also covers a
    // second applet instance (desktop + panel) asking for the same headphone -
    // the device serves a single control connection at a time.
    if (qEnvironmentVariableIsSet("MOONDROP_DEBUG")) {
        std::fprintf(stderr, "[guard] connectTo(%s) state=%d transportConnected=%d\n",
                     qPrintable(address), int(m_state), int(m_transport->isConnected()));
    }
    if (address == m_settingsAddress && m_state == Connected && m_transport->isConnected()) {
        Q_EMIT logMessage(moondropTr("Already connected to %1").arg(address));
        clearError();
        return;
    }
    if (address != m_settingsAddress) {
        m_settingsAddress = address;
        Q_EMIT settingsChanged();
    }
    if (channel > 0 && channel != m_settingsChannel) {
        m_settingsChannel = channel;
        Q_EMIT settingsChanged();
    }

    m_channelValidated = false;
    m_channelCandidates.clear();
    if (m_settingsChannel > 0) {
        m_channelCandidates << m_settingsChannel;
    } else {
        // remember the channel that worked last, then probe the well known ones
        const int last = m_settings->value(QStringLiteral("device/lastChannel"), 0).toInt();
        if (last > 0) {
            m_channelCandidates << last;
        }
        m_channelCandidates << 1 << 16; // MOONDROP EDGE, Space Travel
        for (int c = 2; c <= 30; ++c) {
            if (c != 16) {
                m_channelCandidates << c;
            }
        }
    }
    m_channelIndex = 0;
    tryNextChannel();
}

void MoondropDevice::tryNextChannel()
{
    // If BlueZ says the headphone is not connected, there is nothing to talk to:
    // opening the control channel would fail anyway.  Wait for the watcher to
    // report it instead of tying up the adapter with doomed attempts.
    if (m_bluetoothWatcher && m_bluetoothWatcher->address() == m_settingsAddress
        && !m_bluetoothWatcher->path().isEmpty() && !m_bluetoothWatcher->isDeviceConnected()) {
        Q_EMIT logMessage(moondropTr("Waiting for %1 to connect in Bluetooth…").arg(m_settingsAddress));
        setWaitingForHeadphone(true);
        setState(Disconnected);
        return;
    }
    setWaitingForHeadphone(false);

    if (m_channelIndex >= m_channelCandidates.size()) {
        setState(Disconnected);
        setError(moondropTr("Could not find a responding GAIA channel on %1. Is the headphone switched on?")
                     .arg(m_settingsAddress));
        return;
    }
    const int channel = m_channelCandidates.at(m_channelIndex);
    clearError();
    setState(Connecting);
    m_transport->connectToDevice(m_settingsAddress, channel);
    m_probeTimer->start();
}

void MoondropDevice::disconnectDevice()
{
    m_reconnectTimer->stop();
    m_probeTimer->stop();
    const bool wasBusy = busy();
    m_queue.clear();
    m_hasInFlight = false;
    if (wasBusy) {
        Q_EMIT busyChanged();
    }
    m_transport->disconnectFromDevice();
    setState(Disconnected);
}

void MoondropDevice::onTransportConnected()
{
    // Ask for the firmware version through the normal queue: a GAIA device
    // answers immediately, so this doubles as channel validation for the
    // automatic channel scan, and it keeps `busy` truthful from the start.
    m_channelValidated = false;
    Request probe;
    probe.feature = FeatureBasic;
    probe.command = CBasicGetApplicationVersion;
    probe.what = QStringLiteral("channel-probe");
    probe.timeoutMs = 700;
    probe.maxAttempts = 2;
    probe.onTimeout = [this] {
        if (!m_channelValidated) {
            advanceChannel();
        }
    };
    enqueue(probe);
}

void MoondropDevice::advanceChannel()
{
    if (m_channelValidated) {
        return;
    }
    // trying a different channel, so start its "busy" allowance from scratch
    m_busyRetries = 0;
    m_probeTimer->stop();
    m_transport->disconnectFromDevice();
    ++m_channelIndex;
    QTimer::singleShot(0, this, &MoondropDevice::tryNextChannel);
}

void MoondropDevice::onTransportDisconnected()
{
    m_probeTimer->stop();
    const bool wasConnected = (m_state == Connected);
    setState(Disconnected);
    if (!wasConnected || !m_autoReconnect || !m_autoConnect) {
        return;
    }
    // try again, waiting longer each time so a headphone that is switched off
    // does not get hammered
    m_reconnectAttempts++;
    m_reconnectTimer->start(m_reconnectDelayMs);
    m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, 60000);
    Q_EMIT logMessage(moondropTr("Connection lost, retrying in %1 s")
                          .arg(m_reconnectTimer->interval() / 1000));
}

void MoondropDevice::onError(const QString &message)
{
    // While probing for a working channel, connection errors are expected (wrong
    // channel, nothing listening, ...) and are handled here instead of being
    // reported to the user.
    if (m_state == Connecting && !m_channelValidated) {
        // EBUSY: the RFCOMM link is held by somebody else.  Two cases:
        //   * another instance just disconnected and the teardown is still running
        //     (happens right after a plasmashell restart) - a short wait fixes it
        //   * a headphone serves only one RFCOMM connection at a time, so a
        //     running widget or phone app blocks us until it lets go
        if (m_transport->lastErrno() == EBUSY) {
            if (m_busyRetries < 6) {
                ++m_busyRetries;
                Q_EMIT logMessage(moondropTr("Bluetooth control channel is in use, retrying…"));
                QTimer::singleShot(1000, this, &MoondropDevice::tryNextChannel);
                return;
            }
            // one literal so the string extractor sees the whole message
            setError(moondropTr("The control channel of %1 is already in use by another program "
                                "(for example another Plasma widget, or the phone app). Disconnect there first - the headphone accepts only one connection at a time.")
                         .arg(m_settingsAddress));
            setState(Disconnected);
            if (m_autoReconnect && m_autoConnect) {
                // try again later; whoever holds the link may let go of it
                m_reconnectAttempts++;
                m_reconnectTimer->start(m_reconnectDelayMs);
                m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, 60000);
            }
            return;
        }
        Q_EMIT logMessage(message);
        advanceChannel();
        return;
    }
    setError(message);
}

void MoondropDevice::onReconnectTimer()
{
    if (m_state != Disconnected || !m_autoReconnect || !m_autoConnect || m_settingsAddress.isEmpty()) {
        return;
    }
    Q_EMIT logMessage(moondropTr("Reconnecting…"));
    connectTo(m_settingsAddress, m_settingsChannel);
}

void MoondropDevice::onDataReceived(const QByteArray &data)
{
    const QList<Frame> frames = m_stream.feed(data);
    for (const Frame &frame : frames) {
        handleFrame(frame);
    }
}

void MoondropDevice::handleFrame(const Frame &frame)
{
    Q_EMIT logMessage(QStringLiteral("RX ") + frame.toString());

    if (!m_transport->isConnected()) {
        // stale data from a link that is already gone: it must not validate the
        // channel or change any state
        Q_EMIT logMessage(moondropTr("Ignoring data received while not connected"));
        return;
    }

    if (m_state == Connecting && !m_channelValidated) {
        // first answer means the channel is a GAIA channel
        clearError();
        m_channelValidated = true;
        m_probeTimer->stop();
        m_reconnectDelayMs = 2000;
        m_reconnectAttempts = 0;
        m_settings->setValue(QStringLiteral("device/lastChannel"), m_transport->channel());
        setState(Connected);
        handshake();
    }

    if (frame.type() == TypeNotification) {
        handleNotification(frame);
        return;
    }
    handleResponse(frame);
}

void MoondropDevice::handleResponse(const Frame &frame)
{
    switch (frame.feature()) {
    case FeatureBasic:
        switch (frame.cmd()) {
        case CBasicGetGaiaVersion:
            if (frame.payload.size() >= 2) {
                m_gaiaVersion = QStringLiteral("%1.%2")
                                    .arg(quint8(frame.payload.at(0)))
                                    .arg(quint8(frame.payload.at(1)));
                Q_EMIT infoChanged();
            }
            break;
        case CBasicGetSupportedFeatures:
            parseSupportedFeatures(frame.payload);
            break;
        case CBasicGetSerialNumber:
            m_serial = QString::fromLatin1(frame.payload).trimmed();
            Q_EMIT infoChanged();
            break;
        case CBasicGetVariant:
            m_model = QString::fromLatin1(frame.payload).trimmed();
            applyProfile(m_model);
            updatePresetNames();
            Q_EMIT infoChanged();
            break;
        case CBasicGetApplicationVersion:
            m_firmware = QString::fromLatin1(frame.payload).trimmed();
            Q_EMIT infoChanged();
            break;
        default:
            break;
        }
        break;

    case FeatureBattery:
        if (frame.cmd() == CBatteryGetSupported) {
            // ask for the levels of every supported battery id
            QByteArray ids;
            for (int i = 0; i < frame.payload.size(); ++i) {
                ids.append(frame.payload.at(i));
            }
            if (ids.isEmpty()) {
                for (int id : m_profile.batteryIds) {
                    ids.append(char(id));
                }
            }
            send(FeatureBattery, CBatteryGetLevels, ids, true, QStringLiteral("battery"));
        } else if (frame.cmd() == CBatteryGetLevels) {
            parseBattery(frame.payload);
        }
        break;

    case FeatureAnc:
        if (frame.cmd() == CAncGetCurrentMode || frame.cmd() == CAncGetState) {
            m_ancPath = AncPathAncV1;
            if (!frame.payload.isEmpty()) {
                m_ancMode = (quint8(frame.payload.at(0)) == 0) ? AncOff : AncNoiseCancelling;
            }
            Q_EMIT capabilitiesChanged();
            Q_EMIT ancModeChanged();
        }
        break;

    case FeatureAncV2:
        if (frame.cmd() == CAncV2GetCurrentMode) {
            m_ancPath = AncPathAncV2;
            if (!frame.payload.isEmpty()) {
                m_ancMode = quint8(frame.payload.at(0));
            }
            Q_EMIT capabilitiesChanged();
            Q_EMIT ancModeChanged();
        }
        break;

    case FeatureAudioCuration:
        switch (frame.cmd()) {
        case CAcGetCurrentMode:
            m_ancPath = AncPathAudioCuration;
            if (!frame.payload.isEmpty()) {
                const int value = quint8(frame.payload.at(0));
                // 0 = off, 1 = ANC, 2 = transparency, 3 = wind
                m_ancMode = qBound(0, value, 5);
            }
            Q_EMIT capabilitiesChanged();
            Q_EMIT ancModeChanged();
            break;
        case CAcSetMode:
            Q_EMIT commandFinished(QStringLiteral("anc"), true);
            break;
        default:
            break;
        }
        break;

    case FeatureMusicProcessing:
        switch (frame.cmd()) {
        case CEqGetPresets:
            parsePresets(frame.payload);
            break;
        case CEqGetSet:
            parseSelectedPreset(frame.payload);
            break;
        case CEqGetBandCount:
            parseBandCount(frame.payload);
            break;
        case CEqGetUserConfig:
            parseUserEq(frame.payload);
            break;
        case CEqSelectSet:
            if (!frame.payload.isEmpty()) {
                m_currentPreset = quint8(frame.payload.at(0));
            }
            Q_EMIT eqChanged();
            Q_EMIT commandFinished(QStringLiteral("preset"), true);
            break;
        default:
            break;
        }
        m_eqSupported = true;
        Q_EMIT capabilitiesChanged();
        break;

    case FeatureCodecType:
        parseCodec(frame.feature(), frame.cmd(), frame.payload);
        break;

    case FeatureDacGain:
        if (frame.cmd() == CDacGetGain) {
            m_dacGainSupported = true;
            if (!frame.payload.isEmpty()) {
                m_dacGain = quint8(frame.payload.at(0));
            }
            Q_EMIT capabilitiesChanged();
            Q_EMIT codecChanged();
        }
        break;

    case FeatureOneBringTwo:
        if (frame.cmd() == CMultipointGet) {
            m_multipointSupported = true;
            if (!frame.payload.isEmpty()) {
                m_multipointEnabled = quint8(frame.payload.at(0)) != 0;
            }
            Q_EMIT capabilitiesChanged();
            Q_EMIT codecChanged();
        }
        break;

    case FeatureBtAddress:
        if (frame.cmd() == CBtAddressGet) {
            m_hostAddress = macFromBytes(frame.payload);
            Q_EMIT infoChanged();
        }
        break;

    default:
        break;
    }

    // release the queue when the answer to the in-flight request arrived
    if (m_hasInFlight && m_inFlight.feature == frame.feature() && m_inFlight.command == frame.cmd()
        && frame.type() != TypeNotification) {
        m_hasInFlight = false;
        // the command worked, so any earlier complaint is stale
        clearError();
        if (m_inFlight.onDone) {
            m_inFlight.onDone();
        }
        Q_EMIT busyChanged();
        Q_EMIT commandFinished(m_inFlight.what, true);
        pumpQueue();
    }
}

bool MoondropDevice::notificationRefreshAllowed(quint16 feature)
{
    // Reacting to a notification with a read can make the device emit another
    // notification.  Without a floor between the reactions that turns into an
    // endless exchange which keeps the queue (and therefore `busy`) filled.
    constexpr qint64 minimumIntervalMs = 1500;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_lastNotificationRefresh.value(feature, 0);
    if (now - last < minimumIntervalMs) {
        return false;
    }
    m_lastNotificationRefresh.insert(feature, now);
    return true;
}

void MoondropDevice::handleNotification(const Frame &frame)
{
    switch (frame.feature()) {
    case FeatureAudioCuration:
        if (frame.cmd() == CAcGetCurrentMode && !frame.payload.isEmpty()) {
            m_ancMode = qBound(0, int(quint8(frame.payload.at(0))), 5);
            Q_EMIT ancModeChanged();
        }
        break;
    case FeatureMusicProcessing:
        // EQ state / preset changed elsewhere (e.g. a button on the headphone)
        if (notificationRefreshAllowed(FeatureMusicProcessing)) {
            send(FeatureMusicProcessing, CEqGetSet, QByteArray(), true, QStringLiteral("eq-set"));
            send(FeatureMusicProcessing, CEqGetUserConfig, QByteArray("\x00\x04", 2), true,
                 QStringLiteral("eq-config"));
        }
        break;
    case FeatureBattery:
        // the device pushed a battery update - it may already carry the levels
        if (frame.cmd() == 1 && frame.payload.size() >= 2) {
            parseBattery(frame.payload);
        } else if (notificationRefreshAllowed(FeatureBattery)) {
            QByteArray ids;
            for (int id : m_profile.batteryIds) {
                ids.append(char(id));
            }
            send(FeatureBattery, CBatteryGetLevels, ids, true, QStringLiteral("battery"));
        }
        break;
    default:
        break;
    }
}

void MoondropDevice::writeNow(quint16 feature, quint8 command, const QByteArray &payload)
{
    m_transport->write(encodeFrame(feature, command, payload));
}

void MoondropDevice::send(quint16 feature, quint8 command, const QByteArray &payload, bool expectReply, const QString &what)
{
    Request request;
    request.feature = feature;
    request.command = command;
    request.payload = payload;
    request.expectReply = expectReply;
    request.what = what.isEmpty() ? QStringLiteral("f%1 c%2").arg(feature).arg(command) : what;
    request.timeoutMs = expectReply ? 900 : 250;
    request.maxAttempts = expectReply ? 2 : 1;
    enqueue(request);
}

void MoondropDevice::enqueue(const Request &request)
{
    if (!m_transport->isConnected()) {
        // Nothing to talk to.  Queueing would leave the request (and `busy`)
        // pending forever, which would make the UI refuse further actions.
        if (!request.what.isEmpty()) {
            Q_EMIT logMessage(moondropTr("Not connected, ignoring %1").arg(request.what));
        }
        return;
    }
    m_queue.enqueue(request);
    Q_EMIT busyChanged();
    pumpQueue();
}

// User initiated commands should not queue up behind a background poll.
void MoondropDevice::prepareUserAction()
{
    m_queue.clear();
    Q_EMIT busyChanged();
}

void MoondropDevice::pumpQueue()
{
    while (!m_hasInFlight && !m_queue.isEmpty() && m_transport->isConnected()) {
        m_inFlight = m_queue.dequeue();
        Q_EMIT busyChanged();
        if (m_inFlight.barrier) {
            // "everything queued before me has been handled"
            const Request barrier = m_inFlight;
            m_hasInFlight = false;
            if (barrier.onDone) {
                barrier.onDone();
            }
            continue;
        }
        m_inFlight.attempts = 1;
        m_inFlight.sentAt = QDateTime::currentMSecsSinceEpoch();
        m_hasInFlight = true;
        if (m_inFlight.silent) {
            // nothing to send: this slot only keeps the queue busy (the firmware
            // ignores commands while it is busy switching the ANC engine)
            continue;
        }
        const QByteArray frame = encodeFrame(m_inFlight.feature, m_inFlight.command, m_inFlight.payload);
        m_transport->write(frame);
        Q_EMIT logMessage(QStringLiteral("TX %1 (%2)").arg(QString::fromLatin1(frame.toHex(' ')), m_inFlight.what));
    }
}

void MoondropDevice::onRequestTick()
{
    if (m_hasInFlight && m_inFlight.silent) {
        // busy wait slot: no retry, just release the queue after the delay
        if (QDateTime::currentMSecsSinceEpoch() - m_inFlight.sentAt >= m_inFlight.timeoutMs) {
            const Request done = m_inFlight;
            m_hasInFlight = false;
            Q_EMIT busyChanged();
            if (done.onDone) {
                done.onDone();
            }
            pumpQueue();
        }
        return;
    }
    if (!m_hasInFlight) {
        pumpQueue();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_inFlight.sentAt < m_inFlight.timeoutMs) {
        return;
    }
    if (m_inFlight.attempts < m_inFlight.maxAttempts) {
        m_inFlight.attempts++;
        m_inFlight.sentAt = now;
        m_transport->write(encodeFrame(m_inFlight.feature, m_inFlight.command, m_inFlight.payload));
        Q_EMIT logMessage(moondropTr("retrying %1").arg(m_inFlight.what));
        return;
    }
    const Request failed = m_inFlight;
    m_hasInFlight = false;
    Q_EMIT busyChanged();
    Q_EMIT logMessage(moondropTr("no answer for %1 (feature %2, command %3)")
                          .arg(failed.what)
                          .arg(failed.feature)
                          .arg(failed.command));
    if (failed.onTimeout) {
        failed.onTimeout();
    }
    if (failed.onDone) {
        failed.onDone();
    }
    pumpQueue();
}

bool MoondropDevice::hasFeature(quint16 feature) const
{
    if (!m_featuresKnown) {
        return true; // capability list unknown: try anyway
    }
    return m_supportedFeatures.contains(feature);
}

void MoondropDevice::handshake()
{
    Q_EMIT logMessage(moondropTr("Connected to %1 on channel %2").arg(m_transport->address()).arg(m_transport->channel()));
    refresh();
}

void MoondropDevice::refresh()
{
    if (!m_transport->isConnected()) {
        return;
    }
    m_queue.clear();
    m_hasInFlight = false;

    // Ask which features the device has first and only query those afterwards:
    // the firmware silently ignores commands it does not implement, which would
    // otherwise cost a timeout each.
    Request features;
    features.feature = FeatureBasic;
    features.command = CBasicGetSupportedFeatures;
    features.what = QStringLiteral("supported-features");
    features.onTimeout = [this] {
        m_featuresKnown = false;
        refreshDetails();
    };
    enqueue(features);
}

void MoondropDevice::refreshDetails()
{
    if (!m_transport->isConnected()) {
        return;
    }
    if (m_ready) {
        m_ready = false;
        Q_EMIT readyChanged();
    }
    send(FeatureBasic, CBasicGetGaiaVersion, QByteArray(), true, QStringLiteral("gaia-version"));
    send(FeatureBasic, CBasicGetVariant, QByteArray(), true, QStringLiteral("model"));
    send(FeatureBasic, CBasicGetApplicationVersion, QByteArray(), true, QStringLiteral("firmware"));
    send(FeatureBasic, CBasicGetSerialNumber, QByteArray(), false, QStringLiteral("serial"));
    send(FeatureBtAddress, CBtAddressGet, QByteArray(), false, QStringLiteral("host-address"));

    if (hasFeature(FeatureBattery)) {
        Request battery;
        battery.feature = FeatureBattery;
        battery.command = CBatteryGetSupported;
        battery.what = QStringLiteral("battery-supported");
        battery.onTimeout = [this] {
            // the device does not list its batteries: ask for the profile's ids
            QByteArray ids;
            for (int id : m_profile.batteryIds) {
                ids.append(char(id));
            }
            send(FeatureBattery, CBatteryGetLevels, ids, true, QStringLiteral("battery"));
        };
        enqueue(battery);
    }
    if (hasFeature(FeatureMusicProcessing)) {
        send(FeatureMusicProcessing, CEqGetPresets, QByteArray(), true, QStringLiteral("eq-presets"));
        send(FeatureMusicProcessing, CEqGetSet, QByteArray(), true, QStringLiteral("eq-set"));
        send(FeatureMusicProcessing, CEqGetUserConfig, QByteArray("\x00\x04", 2), true, QStringLiteral("eq-config"));
    }
    if (hasFeature(FeatureCodecType)) {
        send(FeatureCodecType, CCodecGetLdac, QByteArray(), true, QStringLiteral("ldac"));
    }
    if (hasFeature(FeatureDacGain)) {
        send(FeatureDacGain, CDacGetGain, QByteArray(), true, QStringLiteral("dac-gain"));
    }
    if (hasFeature(FeatureOneBringTwo)) {
        send(FeatureOneBringTwo, CMultipointGet, QByteArray(), false, QStringLiteral("multipoint"));
    }

    // noise cancelling exists in three command families: the profile knows which
    // one the model answers, otherwise ask the supported one
    if (m_profile.ancPath == 8 && hasFeature(FeatureAudioCuration)) {
        Request anc;
        anc.feature = FeatureAudioCuration;
        anc.command = CAcGetCurrentMode;
        anc.what = QStringLiteral("anc-mode");
        anc.onTimeout = [this] { probeOtherAncPaths(); };
        enqueue(anc);
    } else if (m_profile.ancPath == 32 && hasFeature(FeatureAncV2)) {
        Request anc;
        anc.feature = FeatureAncV2;
        anc.command = CAncV2GetCurrentMode;
        anc.what = QStringLiteral("anc-v2-mode");
        anc.onTimeout = [this] { probeOtherAncPaths(); };
        enqueue(anc);
    } else if (m_profile.ancPath == 2 && hasFeature(FeatureAnc)) {
        Request anc;
        anc.feature = FeatureAnc;
        anc.command = CAncGetCurrentMode;
        anc.what = QStringLiteral("anc-v1-mode");
        anc.onTimeout = [this] { probeOtherAncPaths(); };
        enqueue(anc);
    } else if (hasFeature(FeatureAudioCuration)) {
        Request anc;
        anc.feature = FeatureAudioCuration;
        anc.command = CAcGetCurrentMode;
        anc.what = QStringLiteral("anc-mode");
        anc.onTimeout = [this] { probeOtherAncPaths(); };
        enqueue(anc);
    } else {
        probeOtherAncPaths();
    }

    // barrier: reaching it means every request above was answered or timed out
    Request done;
    done.barrier = true;
    done.what = QStringLiteral("refresh-done");
    done.onDone = [this] {
        if (!m_ready) {
            m_ready = true;
            Q_EMIT readyChanged();
        }
    };
    enqueue(done);
}

void MoondropDevice::probeOtherAncPaths()
{
    if (m_ancPath != AncPathUnknown || !m_transport->isConnected()) {
        return;
    }
    if (hasFeature(FeatureAncV2)) {
        send(FeatureAncV2, CAncV2GetCurrentMode, QByteArray(), true, QStringLiteral("anc-v2-mode"));
    } else if (hasFeature(FeatureAnc)) {
        send(FeatureAnc, CAncGetCurrentMode, QByteArray(), true, QStringLiteral("anc-v1-mode"));
    }
}

// ---------------------------------------------------------------------------
// parsing
// ---------------------------------------------------------------------------

void MoondropDevice::parseBattery(const QByteArray &payload)
{
    if (payload.size() < 2) {
        return;
    }
    // The payload is a list of (battery id, level) pairs.  Which levels are
    // meaningful depends on the model: on TWS models 0 means "not connected"
    // (the bud sits in the case) and 0xFF means "unreadable", while a charging
    // case may legitimately report 0 %.
    QVariantList list;
    int first = -1;
    int left = -1;
    int right = -1;
    int box = -1;
    bool anyValid = false;
    for (int i = 0; i + 1 < payload.size(); i += 2) {
        const int id = quint8(payload.at(i));
        const int level = quint8(payload.at(i + 1));
        const bool valid = batteryLevelIsValid(m_profile.battery, id, level);
        anyValid = anyValid || valid;

        QVariantMap entry;
        entry.insert(QStringLiteral("id"), id);
        entry.insert(QStringLiteral("level"), valid ? level : -1);
        entry.insert(QStringLiteral("valid"), valid);
        list.append(entry);

        if (valid) {
            if (first < 0) {
                first = level;
            }
            if (id == 1 || (id == 0 && !m_profile.isBuds)) {
                left = level;
            } else if (id == 2) {
                right = level;
            } else if (isCaseBattery(id)) {
                box = level;
            }
        }
    }
    if (!anyValid) {
        // everything unreadable: keep the last known values instead of showing 0 %
        m_batteries = list;
        Q_EMIT batteryChanged();
        return;
    }

    m_batteries = list;
    // For earbuds the interesting number is the *weaker* one: showing the better
    // bud hides the fact that the other is nearly empty.  Headphones report a
    // single value and keep it as is.
    if (left >= 0 && right >= 0) {
        m_batteryLevel = qMin(left, right);
    } else if (left >= 0 || right >= 0) {
        m_batteryLevel = qMax(left, right);
    } else if (box >= 0) {
        m_batteryLevel = box;
    } else {
        m_batteryLevel = first;
    }
    Q_EMIT batteryChanged();
}

void MoondropDevice::parsePresets(const QByteArray &payload)
{
    if (payload.isEmpty()) {
        return;
    }
    // first byte is the number of available presets, followed by their ids
    int count = quint8(payload.at(0));
    if (count > payload.size() - 1) {
        count = payload.size() - 1;
    }
    QVariantList ids;
    for (int i = 1; i <= count; ++i) {
        ids.append(int(quint8(payload.at(i))));
    }
    if (ids.isEmpty()) {
        // some firmwares answer with a plain list of ids
        for (int i = 0; i < payload.size(); ++i) {
            ids.append(int(quint8(payload.at(i))));
        }
    }
    m_presetIds = ids;
    m_eqSupported = true;
    updatePresetNames();
    Q_EMIT eqChanged();
    Q_EMIT capabilitiesChanged();
}

void MoondropDevice::parseSelectedPreset(const QByteArray &payload)
{
    if (payload.isEmpty()) {
        return;
    }
    m_currentPreset = quint8(payload.at(0));
    Q_EMIT eqChanged();
}

void MoondropDevice::parseBandCount(const QByteArray &payload)
{
    if (payload.isEmpty()) {
        return;
    }
    Q_EMIT eqChanged();
}

void MoondropDevice::rememberDeviceEq(const QVariantList &bands)
{
    if (bands.size() != 5) {
        return;
    }
    m_deviceEqBackup = bands;
    // keep it across restarts: a curve lost to an accidental change is annoying
    QJsonArray array;
    for (const QVariant &item : bands) {
        const QVariantMap band = item.toMap();
        QJsonObject object;
        object.insert(QStringLiteral("frequency"), band.value(QStringLiteral("frequency")).toInt());
        object.insert(QStringLiteral("gain"), band.value(QStringLiteral("gain")).toDouble());
        object.insert(QStringLiteral("q"), band.value(QStringLiteral("q")).toDouble());
        object.insert(QStringLiteral("type"), band.value(QStringLiteral("type")).toInt());
        array.append(object);
    }
    m_settings->setValue(QStringLiteral("eq/deviceBackup"),
                         QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    m_settings->sync();
}

void MoondropDevice::restoreDeviceEq()
{
    if (m_deviceEqBackup.isEmpty()) {
        setError(moondropTr("There is no saved curve to restore."));
        return;
    }
    // by value, and without taking a new snapshot: the restore point stays
    applyUserEqInternal(QVariantList(m_deviceEqBackup), true, false);
}

void MoondropDevice::parseUserEq(const QByteArray &payload)
{
    const QList<EqBand> bands = decodeUserEq(payload);
    if (bands.isEmpty()) {
        return;
    }
    QVariantList list;
    for (const EqBand &band : bands) {
        QVariantMap entry;
        entry.insert(QStringLiteral("frequency"), band.frequency);
        entry.insert(QStringLiteral("q"), band.q);
        entry.insert(QStringLiteral("gain"), band.gain);
        entry.insert(QStringLiteral("type"), band.type);
        list.append(entry);
    }
    // A read that is not the verification of our own write is the curve the
    // headphone itself is happy with - that is the one worth remembering.
    if (m_eqWriteTarget.isEmpty()) {
        rememberDeviceEq(list);
    }
    m_bands = list;
    Q_EMIT eqChanged();

    if (m_eqWriteTarget.size() == 5) {
        if (bandsMatch(m_bands, m_eqWriteTarget)) {
            m_eqWriteTarget.clear();
            Q_EMIT eqChanged();
            Q_EMIT commandFinished(QStringLiteral("peq-write"), true);
        } else if (m_eqWriteAttempts < 2) {
            ++m_eqWriteAttempts;
            Q_EMIT logMessage(moondropTr("PEQ write not confirmed, writing again…"));
            writeEqPayload(m_eqWriteTarget);
            Request verify;
            verify.feature = FeatureMusicProcessing;
            verify.command = CEqGetUserConfig;
            verify.payload = QByteArray("\x00\x04", 2);
            verify.what = QStringLiteral("eq-config");
            enqueue(verify);
        } else {
            m_eqWriteTarget.clear();
            Q_EMIT eqChanged();
            Q_EMIT commandFinished(QStringLiteral("peq-write"), false);
            setError(moondropTr("The headphone did not accept the parametric EQ."));
        }
    }
}

void MoondropDevice::parseCodec(quint16 feature, quint8 command, const QByteArray &payload)
{
    Q_UNUSED(feature)
    const bool enabled = payload.isEmpty() ? false : (quint8(payload.at(0)) != 0);
    switch (command) {
    case CCodecGetLc3:
        m_lc3Supported = true;
        m_lc3Enabled = enabled;
        break;
    case CCodecGetLdac:
        m_ldacSupported = true;
        m_ldacEnabled = enabled;
        break;
    case CCodecGetLhdc:
        m_lhdcSupported = true;
        m_lhdcEnabled = enabled;
        break;
    case CCodecSetLc3:
    case CCodecSetLdac:
    case CCodecSetLhdc:
        break;
    default:
        return;
    }
    Q_EMIT capabilitiesChanged();
    Q_EMIT codecChanged();
}

void MoondropDevice::parseDacGain(const QByteArray &payload)
{
    if (payload.isEmpty()) {
        return;
    }
    m_dacGain = quint8(payload.at(0));
    Q_EMIT codecChanged();
}

void MoondropDevice::parseMultipoint(const QByteArray &payload)
{
    if (payload.isEmpty()) {
        return;
    }
    m_multipointEnabled = quint8(payload.at(0)) != 0;
    Q_EMIT codecChanged();
}

void MoondropDevice::setWaitingForHeadphone(bool waiting)
{
    if (m_waitingForHeadphone == waiting) {
        return;
    }
    m_waitingForHeadphone = waiting;
    Q_EMIT waitingChanged();
}

QString MoondropDevice::profileNotes() const
{
    return Moondrop::profileNotes(m_profile);
}

QString MoondropDevice::profileName() const
{
    if (m_profile.id == QLatin1String("unknown") && !m_model.isEmpty()) {
        return m_model;
    }
    return profileDisplayName(m_profile);
}

QString MoondropDevice::profileOverride() const
{
    if (!m_profileOverride.isEmpty()) {
        return m_profileOverride;
    }
    return m_settings->value(QStringLiteral("device/profile"), QStringLiteral("auto")).toString();
}

QVariantList MoondropDevice::availableProfiles() const
{
    QVariantList list;
    QVariantMap automatic;
    automatic.insert(QStringLiteral("id"), QStringLiteral("auto"));
    automatic.insert(QStringLiteral("label"), moondropTr("Detect automatically"));
    automatic.insert(QStringLiteral("verified"), false);
    list.append(automatic);
    for (const DeviceProfile &profile : deviceProfiles()) {
        if (profile.id == QLatin1String("unknown")) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), profile.id);
        QString label = profileDisplayName(profile);
        if (profile.verified) {
            label += QLatin1String("  (") + moondropTr("verified") + QLatin1Char(')');
        }
        entry.insert(QStringLiteral("label"), label);
        entry.insert(QStringLiteral("verified"), profile.verified);
        list.append(entry);
    }
    return list;
}

void MoondropDevice::setProfileOverride(const QString &profileId)
{
    m_profileOverride = profileId;
    m_settings->setValue(QStringLiteral("device/profile"), profileId);
    m_settings->sync();
    // force a re-apply even when the resolved profile stays the same
    m_profile = DeviceProfile();
    applyProfile(m_model);
    Q_EMIT profileChanged();
}

void MoondropDevice::applyProfile(const QString &modelName)
{
    QString wanted = m_profileOverride;
    if (wanted.isEmpty()) {
        wanted = m_settings->value(QStringLiteral("device/profile"), QString()).toString();
    }
    DeviceProfile picked;
    if (!wanted.isEmpty() && wanted != QLatin1String("auto")) {
        for (const DeviceProfile &profile : deviceProfiles()) {
            if (profile.id == wanted) {
                picked = profile;
                break;
            }
        }
    }
    if (picked.id.isEmpty()) {
        picked = matchProfile(modelName);
    }
    if (picked.id == m_profile.id && picked.ancPath == m_profile.ancPath
        && picked.battery == m_profile.battery) {
        return;
    }
    m_profile = picked;
    Q_EMIT profileChanged();
    Q_EMIT capabilitiesChanged();
}

void MoondropDevice::parseSupportedFeatures(const QByteArray &payload)
{
    // The response is a list of (feature id, feature version) pairs.  Some
    // firmwares prefix the list with a status byte, so accept both layouts.
    QList<int> features;
    if (!payload.isEmpty() && payload.size() % 2 == 0) {
        for (int i = 0; i + 1 < payload.size(); i += 2) {
            features.append(quint8(payload.at(i)));
        }
    } else {
        for (int i = 1; i + 1 < payload.size(); i += 2) {
            features.append(quint8(payload.at(i)));
        }
    }
    m_supportedFeatures.clear();
    QVariantList list;
    for (int feature : features) {
        if (feature < 0 || feature > 64) {
            continue;
        }
        m_supportedFeatures.insert(quint16(feature));
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), feature);
        entry.insert(QStringLiteral("name"), Moondrop::featureName(quint16(feature)));
        list.append(entry);
    }
    m_features = list;
    m_featuresKnown = true;
    Q_EMIT infoChanged();
    Q_EMIT capabilitiesChanged();
    refreshDetails();
}

// ---------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------

void MoondropDevice::sendRaw(int feature, int command, const QString &payloadHex, bool expectReply)
{
    const QByteArray payload = QByteArray::fromHex(payloadHex.toLatin1());
    send(quint16(feature), quint8(command), payload, expectReply,
         QStringLiteral("raw f%1 c%2").arg(feature).arg(command));
}

void MoondropDevice::setAncMode(int mode)
{
    if (!m_transport->isConnected()) {
        return;
    }
    int deviceValue = -1;
    switch (m_ancPath) {
    case AncPathAudioCuration:
        // MOONDROP firmware expects bit style codes here: 1 = off, 2 = ANC,
        // 3 = wind, 4 = transparency
        switch (mode) {
        case AncOff: deviceValue = 1; break;
        case AncNoiseCancelling: deviceValue = 2; break;
        case AncTransparency: deviceValue = 4; break;
        case AncWind: deviceValue = 3; break;
        default: deviceValue = -1; break;
        }
        break;
    case AncPathAncV2:
        deviceValue = mode;
        break;
    case AncPathAncV1:
        deviceValue = (mode == AncOff) ? 0 : 1;
        break;
    default:
        setError(moondropTr("This headphone does not report noise cancelling support."));
        return;
    }
    if (deviceValue < 0) {
        return;
    }

    const quint16 feature = (m_ancPath == AncPathAudioCuration) ? FeatureAudioCuration
                            : (m_ancPath == AncPathAncV2)      ? FeatureAncV2
                                                               : FeatureAnc;
    const quint8 command = (m_ancPath == AncPathAudioCuration) ? CAcSetMode
                           : (m_ancPath == AncPathAncV2)       ? CAncV2SetCurrentMode
                                                               : CAncGetState;
    // The firmware does not acknowledge this command, so update optimistically
    // and queue a verification read right behind it (the queue keeps the order).
    // Switching the ANC engine keeps the firmware busy for up to two seconds,
    // it ignores everything sent in the meantime - hence the long slot.
    prepareUserAction();
    // write right away (the device does not acknowledge it at all) ...
    writeNow(feature, command, QByteArray(1, char(deviceValue)));
    // ... and keep the queue busy for the time the firmware needs to settle, it
    // ignores everything sent in the meantime
    Request settle;
    settle.silent = true;
    settle.timeoutMs = 2200;
    settle.maxAttempts = 1;
    settle.what = QStringLiteral("anc-settle");
    enqueue(settle);
    m_ancMode = mode;
    Q_EMIT ancModeChanged();

    Request verify;
    verify.feature = FeatureAudioCuration;
    verify.command = CAcGetCurrentMode;
    verify.what = QStringLiteral("anc-mode");
    verify.timeoutMs = 1000;
    verify.maxAttempts = 3;
    enqueue(verify);
}

void MoondropDevice::selectPreset(int presetId)
{
    prepareUserAction();
    enqueuePresetSelect(presetId);
}

// Queues the preset selection without touching the queue, so it can be
// appended after a PEQ write and stay in order.
void MoondropDevice::enqueuePresetSelect(int presetId)
{
    send(FeatureMusicProcessing, CEqSelectSet, QByteArray(1, char(presetId)), true, QStringLiteral("preset"));
    m_currentPreset = presetId;
    Q_EMIT eqChanged();

    Request verify;
    verify.feature = FeatureMusicProcessing;
    verify.command = CEqGetSet;
    verify.what = QStringLiteral("eq-set");
    enqueue(verify);
}

void MoondropDevice::requestUserEq()
{
    send(FeatureMusicProcessing, CEqGetUserConfig, QByteArray("\x00\x04", 2), true, QStringLiteral("eq-config"));
}

void MoondropDevice::applyUserEq(const QVariantList &bands, bool selectCustom)
{
    // Copy: the caller may pass m_deviceEqBackup itself (restoreDeviceEq does),
    // and the snapshot below would then replace the very curve we are writing.
    applyUserEqInternal(bands, selectCustom, true);
}

void MoondropDevice::applyUserEqInternal(const QVariantList &bands, bool selectCustom, bool snapshot)
{
    if (bands.size() != 5) {
        setError(moondropTr("The parametric EQ needs exactly 5 bands."));
        return;
    }
    prepareUserAction();
    // anything we are about to overwrite becomes the restore point
    if (snapshot && m_bands.size() == 5 && !bandsMatch(m_bands, bands)) {
        rememberDeviceEq(m_bands);
    }
    m_eqWriteTarget = bands;
    m_eqWriteAttempts = 0;
    writeEqPayload(bands);

    if (selectCustom) {
        // appended after the write, which is still queued behind the in-flight
        // request (if any) - so the write can never be overtaken
        enqueuePresetSelect(UserPresetId);
    }

    Request verify;
    verify.feature = FeatureMusicProcessing;
    verify.command = CEqGetUserConfig;
    verify.payload = QByteArray("\x00\x04", 2);
    verify.what = QStringLiteral("eq-config");
    enqueue(verify);
}

void MoondropDevice::writeEqPayload(const QVariantList &bands)
{
    QList<EqBand> list;
    for (const QVariant &item : bands) {
        const QVariantMap map = item.toMap();
        EqBand band;
        band.frequency = map.value(QStringLiteral("frequency"), 1000).toInt();
        band.q = map.value(QStringLiteral("q"), 1.0).toDouble();
        band.gain = map.value(QStringLiteral("gain"), 0.0).toDouble();
        band.type = map.value(QStringLiteral("type"), 0).toInt();
        list.append(band);
    }
    const QByteArray payload = encodeUserEq(list);
    if (payload.isEmpty()) {
        setError(moondropTr("Could not encode the parametric EQ."));
        return;
    }
    Request write;
    write.feature = FeatureMusicProcessing;
    write.command = CEqSetUserConfig;
    write.payload = payload;
    write.expectReply = false;
    write.timeoutMs = 800; // the firmware needs a moment for a 39 byte write
    write.maxAttempts = 1;
    write.what = QStringLiteral("peq-write");
    enqueue(write);
    Q_EMIT eqChanged();
}

bool MoondropDevice::bandsMatch(const QVariantList &a, const QVariantList &b)
{
    if (a.size() != b.size() || a.isEmpty()) {
        return false;
    }
    for (int i = 0; i < a.size(); ++i) {
        const QVariantMap left = a.at(i).toMap();
        const QVariantMap right = b.at(i).toMap();
        if (left.value(QStringLiteral("frequency")).toInt() != right.value(QStringLiteral("frequency")).toInt()) {
            return false;
        }
        if (qAbs(left.value(QStringLiteral("gain")).toDouble() - right.value(QStringLiteral("gain")).toDouble()) > 0.02) {
            return false;
        }
        if (qAbs(left.value(QStringLiteral("q")).toDouble() - right.value(QStringLiteral("q")).toDouble()) > 0.001) {
            return false;
        }
    }
    return true;
}

void MoondropDevice::setLdacEnabled(bool enabled)
{
    prepareUserAction();
    send(FeatureCodecType, CCodecSetLdac, QByteArray(1, char(enabled ? 1 : 0)), false, QStringLiteral("ldac-set"));
    m_ldacEnabled = enabled;
    Q_EMIT codecChanged();

    Request verify;
    verify.feature = FeatureCodecType;
    verify.command = CCodecGetLdac;
    verify.what = QStringLiteral("ldac");
    enqueue(verify);
}

void MoondropDevice::setLc3Enabled(bool enabled)
{
    send(FeatureCodecType, CCodecSetLc3, QByteArray(1, char(enabled ? 1 : 0)), false, QStringLiteral("lc3-set"));
    m_lc3Enabled = enabled;
    Q_EMIT codecChanged();
}

void MoondropDevice::setLhdcEnabled(bool enabled)
{
    send(FeatureCodecType, CCodecSetLhdc, QByteArray(1, char(enabled ? 1 : 0)), false, QStringLiteral("lhdc-set"));
    m_lhdcEnabled = enabled;
    Q_EMIT codecChanged();
}

void MoondropDevice::setDacGain(int gain)
{
    prepareUserAction();
    send(FeatureDacGain, CDacSetGain, QByteArray(1, char(gain & 0xFF)), false, QStringLiteral("dac-set"));
    m_dacGain = gain;
    Q_EMIT codecChanged();

    Request verify;
    verify.feature = FeatureDacGain;
    verify.command = CDacGetGain;
    verify.what = QStringLiteral("dac-gain");
    enqueue(verify);
}

void MoondropDevice::setMultipointEnabled(bool enabled)
{
    send(FeatureOneBringTwo, CMultipointSet, QByteArray(1, char(enabled ? 1 : 0)), false, QStringLiteral("multipoint-set"));
    m_multipointEnabled = enabled;
    Q_EMIT codecChanged();
}

QString MoondropDevice::presetName(int presetId) const
{
    for (int i = 0; i < m_presetIds.size(); ++i) {
        if (m_presetIds.at(i).toInt() == presetId && i < m_presetNames.size()) {
            return m_presetNames.at(i);
        }
    }
    if (presetId == UserPresetId) {
        return moondropTr("Custom PEQ");
    }
    return moondropTr("Preset %1").arg(presetId);
}

void MoondropDevice::updatePresetNames()
{
    // Default naming for the MOONDROP factory presets.  The exact order is not
    // part of the protocol, so it can be overridden in the configuration file
    // (~/.config/moondrop-widget/config.ini -> ui/presetNames).
    static const QStringList factoryDefaults{
        QStringLiteral("Standard"),
        QStringLiteral("Extra Bass"),
        QStringLiteral("Country Style"),
        QStringLiteral("Old Studio Style"),
        QStringLiteral("Violin Solo"),
    };
    QStringList configured = m_settings->value(QStringLiteral("ui/presetNames")).toStringList();
    QStringList names;
    for (const QVariant &id : m_presetIds) {
        const int value = id.toInt();
        if (value == UserPresetId) {
            names << moondropTr("Custom PEQ");
            continue;
        }
        if (value < configured.size() && !configured.at(value).isEmpty()) {
            names << configured.at(value);
        } else if (value < factoryDefaults.size()) {
            names << factoryDefaults.at(value);
        } else {
            names << moondropTr("Preset %1").arg(value);
        }
    }
    if (names != m_presetNames) {
        m_presetNames = names;
        Q_EMIT presetNamesChanged();
    }
}

} // namespace Moondrop
