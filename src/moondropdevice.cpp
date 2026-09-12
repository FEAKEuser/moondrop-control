// SPDX-License-Identifier: GPL-3.0-or-later
#include "moondropdevice.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>

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
    m_injectedTransport = transport != nullptr;
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

    // Deadline for one channel candidate: a channel that accepts the connection
    // but never answers is dropped here instead of after the kernel's own
    // (much longer) timeout.
    m_scanTimer = new QTimer(this);
    m_scanTimer->setSingleShot(true);
    connect(m_scanTimer, &QTimer::timeout, this, &MoondropDevice::onScanTimeout);

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
    // React to *any* MOONDROP headphone appearing, not only the configured one:
    // that is what makes switching pairs connect immediately.
    connect(m_bluetoothWatcher, &BlueZWatcher::headphoneAppeared,
            this, &MoondropDevice::onHeadphoneAppeared);
    m_bluetoothWatcher->watchAllHeadsets(true);
    m_bluetoothWatcher->watch(m_settingsAddress);

    // Everything is wired up and the initial state is read: from here on, a
    // Bluetooth event may start a connection.
    m_constructed = true;

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
    if (!m_startupAutoConnect || !m_autoConnect) {
        return;
    }
    if (m_state != Disconnected) {
        return; // somebody (a tool, or the user) was faster
    }
    if (m_settingsAddress.isEmpty()) {
        // Nothing selected yet.  Rather than making the user open the settings
        // page first, pick the headphone BlueZ already knows about: exactly one
        // MOONDROP device is the common case, and the settings page can still
        // override it.
        const QString candidate = autodetectAddress();
        if (candidate.isEmpty()) {
            Q_EMIT logMessage(moondropTr("No MOONDROP headphone is paired yet - pair it in the "
                                         "system Bluetooth settings."));
            return;
        }
        Q_EMIT logMessage(moondropTr("Using the paired headphone %1").arg(candidate));
        setAddress(candidate);
    }
    Q_EMIT logMessage(moondropTr("Connecting to the headphone…"));
    connectDevice();
}

QString MoondropDevice::connectedMoondropAddress() const
{
    // Address of a MOONDROP headphone that BlueZ currently reports as connected,
    // ignoring the one we were asked about.  Lets the widget follow the headphone
    // the user actually switched on.
    const QDBusMessage reply = QDBusConnection::systemBus().call(
        QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"),
                                       QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                       QStringLiteral("GetManagedObjects")),
        QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return QString();
    }

    QString found;
    const QDBusArgument objects = reply.arguments().at(0).value<QDBusArgument>();
    objects.beginMap();
    while (!objects.atEnd()) {
        objects.beginMapEntry();
        QString path;
        QVariant ignored;
        objects >> path >> ignored;
        objects.endMapEntry();
        if (!path.contains(QLatin1String("/dev_")) || path.count(QLatin1Char('/')) != 4) {
            continue;
        }
        QDBusMessage request =
            QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), path,
                                           QStringLiteral("org.freedesktop.DBus.Properties"),
                                           QStringLiteral("GetAll"));
        request << QStringLiteral("org.bluez.Device1");
        const QDBusMessage deviceReply = QDBusConnection::systemBus().call(request, QDBus::Block, 2000);
        if (deviceReply.type() != QDBusMessage::ReplyMessage || deviceReply.arguments().isEmpty()) {
            continue;
        }
        const QVariantMap props = qdbus_cast<QVariantMap>(deviceReply.arguments().at(0));
        if (!props.value(QStringLiteral("Connected")).toBool()) {
            continue;
        }
        QString name = props.value(QStringLiteral("Alias")).toString();
        if (name.isEmpty()) {
            name = props.value(QStringLiteral("Name")).toString();
        }
        if (!name.contains(QLatin1String("MOONDROP"), Qt::CaseInsensitive)
            && !name.contains(QStringLiteral("水月雨"))) {
            continue;
        }
        found = props.value(QStringLiteral("Address")).toString();
        break;
    }
    objects.endMap();
    return found;
}

QString MoondropDevice::autodetectAddress() const
{
    // Ask BlueZ for its devices.  A single MOONDROP entry is used as is; with
    // several, the connected or, failing that, the trusted one wins, so a user
    // with more than one pair gets a sensible default instead of nothing.
    const QDBusMessage reply = QDBusConnection::systemBus().call(
        QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"),
                                       QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                       QStringLiteral("GetManagedObjects")),
        QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return QString();
    }

    QStringList moondrop;
    QString best;
    QString bestName;
    const QDBusArgument objects = reply.arguments().at(0).value<QDBusArgument>();
    objects.beginMap();
    while (!objects.atEnd()) {
        objects.beginMapEntry();
        QString path;
        QVariant ignored;
        objects >> path >> ignored;
        objects.endMapEntry();
        if (!path.contains(QLatin1String("/dev_")) || path.count(QLatin1Char('/')) != 4) {
            continue;
        }
        QDBusMessage request =
            QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), path,
                                           QStringLiteral("org.freedesktop.DBus.Properties"),
                                           QStringLiteral("GetAll"));
        request << QStringLiteral("org.bluez.Device1");
        const QDBusMessage deviceReply = QDBusConnection::systemBus().call(request, QDBus::Block, 2000);
        if (deviceReply.type() != QDBusMessage::ReplyMessage || deviceReply.arguments().isEmpty()) {
            continue;
        }
        const QVariantMap props = qdbus_cast<QVariantMap>(deviceReply.arguments().at(0));
        QString name = props.value(QStringLiteral("Alias")).toString();
        if (name.isEmpty()) {
            name = props.value(QStringLiteral("Name")).toString();
        }
        if (!name.contains(QLatin1String("MOONDROP"), Qt::CaseInsensitive)
            && !name.contains(QStringLiteral("水月雨"))) {
            continue;
        }
        const QString address = props.value(QStringLiteral("Address")).toString();
        moondrop << address;
        // a device that is actually connected wins, otherwise the first one found
        const bool connected = props.value(QStringLiteral("Connected")).toBool();
        if (connected || best.isEmpty()) {
            best = address;
            bestName = name;
        }
    }
    objects.endMap();
    if (moondrop.size() > 1) {
        Q_EMIT const_cast<MoondropDevice *>(this)->logMessage(
            moondropTr("Several MOONDROP headphones are paired; using %1. Pick another one in the "
                       "widget settings.")
                .arg(bestName));
    }
    return best;
}

void MoondropDevice::onDeviceAppeared(const QString &address)
{
    Q_UNUSED(address)
    setWaitingForHeadphone(false);
    // The watcher reports the device as connected *synchronously* from
    // watch()/resolvePath(), which runs inside the constructor.  Reacting to that
    // first report would connect during construction, before the applet has had a
    // chance to call disableStartupAutoConnect() - tools and the UI checks rely
    // on that to stay away from the headphone's single control channel.
    if (!m_constructed) {
        return;
    }
    if (!m_autoConnect || m_state != Disconnected) {
        return;
    }
    Q_EMIT logMessage(moondropTr("The headphone connected, linking the control channel…"));
    // the A2DP link is up, so our channel should be free
    m_reconnectDelayMs = 2000;
    m_reconnectAttempts = 0;
    connectDevice();
}

void MoondropDevice::onHeadphoneAppeared(const QString &address)
{
    // Another (or the configured) headphone just connected at the Bluetooth level.
    // Retry right away instead of waiting for the next reconnect tick, so a user
    // switching pairs does not have to press anything.
    if (!m_autoConnect || m_state != Disconnected) {
        return;
    }
    if (!m_autoReconnect) {
        return;
    }
    // Retry even when it is the configured headphone coming back: while it was
    // away the backend parked in "waiting for the headphone", and deviceConnected
    // only fires for a state change the watcher observes - not when the user turns
    // the same pair on again after a retry already gave up.  Retrying here (and
    // not only for a *different* device) is what removes the need to press
    // "Retry now".
    if (address != m_settingsAddress) {
        Q_EMIT logMessage(moondropTr("A different headphone (%1) connected; connecting to it.").arg(address));
        setAddress(address);
    } else {
        Q_EMIT logMessage(moondropTr("The headphone is back; connecting again."));
    }
    m_reconnectDelayMs = 2000;
    m_reconnectAttempts = 0;
    m_reconnectTimer->stop();
    connectDevice();
}

void MoondropDevice::onDeviceVanished(const QString &address)
{
    Q_UNUSED(address)
    if (m_autoConnect) {
        setWaitingForHeadphone(true);
    }
    // While no headphone is connected there is nothing to protect, so probe at a
    // short, fixed interval instead of growing towards a minute.  Switching from
    // one pair to another should not feel like "the widget gave up".
    if (m_autoConnect && m_autoReconnect) {
        m_reconnectDelayMs = 2000;
        m_reconnectAttempts = 0;
    }
    if (m_state == Disconnected) {
        // Already down (or never up).  The reconnect timer is what brings us back
        // when *another* headphone is switched on, so make sure it is running:
        // otherwise the user has to press "Retry now" by hand.
        if (m_autoConnect && m_autoReconnect && !m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(m_reconnectDelayMs);
        }
        return;
    }
    Q_EMIT logMessage(moondropTr("The headphone disconnected."));
    m_scanGaveUp = true;
    const bool wasBusy = busy();
    m_queue.clear();
    m_hasInFlight = false;
    if (wasBusy) {
        Q_EMIT busyChanged();
    }
    m_transport->disconnectFromDevice();
    setState(Disconnected);

    // Keep looking instead of stopping here.  The retry path re-runs the channel
    // scan, and that scan follows whichever MOONDROP headphone is connected now
    // (see startChannelScan()), so switching from one pair to another needs no
    // user interaction at all.
    if (m_autoConnect && m_autoReconnect && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start(m_reconnectDelayMs);
        Q_EMIT logMessage(moondropTr("Connection lost, retrying in %1 s")
                              .arg(m_reconnectTimer->interval() / 1000));
    }
}

MoondropDevice::~MoondropDevice()
{
    saveSettings();
    m_scanGaveUp = true; // no late probe callback may touch the transports
    m_transport->disconnectFromDevice();
    if (m_scanTransport) {
        m_scanTransport->disconnectFromDevice();
    }
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
    // Selecting another headphone invalidates everything read from the previous
    // one - without this the UI would describe the old model until the new
    // connection completes its first refresh.
    clearDeviceInfo();
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

void MoondropDevice::clearDeviceInfo()
{
    // Everything below is read *from the headphone* and is therefore only valid
    // while that headphone is the one we are talking to.  Without this the widget
    // keeps showing the previous model after the user switches headphones.
    m_model.clear();
    m_firmware.clear();
    m_serial.clear();
    m_gaiaVersion.clear();
    m_hostAddress.clear();
    m_features.clear();
    m_featuresKnown = false;
    m_peqBandCount = 0;

    // fall back to the pre-detection profile for the configured address (or the
    // unknown one when nothing is selected)
    m_profile = DeviceProfile();
    m_profile.id = QStringLiteral("unknown");
    m_profile.name = QStringLiteral("Unknown device");
    m_profile.ancPath = -1;

    m_batteryLevel = -1;
    m_batteries.clear();
    m_bluetoothBattery = -1;

    m_ancPath = AncPathUnknown;
    m_ancMode = -1;

    m_eqSupported = false;
    m_presetIds.clear();
    m_currentPreset = -1;
    m_bands.clear();
    m_eqWriteTarget.clear();
    m_eqWriteAttempts = 0;

    m_ldacSupported = false;
    m_ldacEnabled = false;
    m_lc3Supported = false;
    m_lc3Enabled = false;
    m_lhdcSupported = false;
    m_lhdcEnabled = false;
    m_dacGainSupported = false;
    m_dacGain = -1;
    m_multipointSupported = false;
    m_multipointEnabled = false;

    // the parser may hold half a frame from the old link
    m_stream.clear();

    if (m_ready) {
        m_ready = false;
        Q_EMIT readyChanged();
    }
    Q_EMIT infoChanged();
    Q_EMIT profileChanged();
    Q_EMIT capabilitiesChanged();
    Q_EMIT batteryChanged();
    Q_EMIT ancModeChanged();
    Q_EMIT eqChanged();
    Q_EMIT codecChanged();
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
    startChannelScan();
}

void MoondropDevice::startChannelScan()
{
    // If BlueZ says the headphone is not connected, there is nothing to talk to:
    // opening the control channel would fail anyway.  Wait for the watcher to
    // report it instead of tying up the adapter with doomed attempts.
    if (m_bluetoothWatcher && m_bluetoothWatcher->address() == m_settingsAddress
        && !m_bluetoothWatcher->path().isEmpty() && !m_bluetoothWatcher->isDeviceConnected()) {
        // The configured headphone is offline, but the user may simply have
        // switched to another one: if a *different* MOONDROP headphone is
        // connected right now, follow that instead of waiting for ever for a
        // device that is switched off.  This is what makes "connect my other
        // pair" work without opening the settings page.
        const QString live = connectedMoondropAddress();
        if (!live.isEmpty() && live != m_settingsAddress) {
            Q_EMIT logMessage(moondropTr("%1 is not connected; switching to %2 which is.")
                                  .arg(m_settingsAddress, live));
            setAddress(live);
            startChannelScan();
            return;
        }
        Q_EMIT logMessage(moondropTr("Waiting for %1 to connect in Bluetooth…").arg(m_settingsAddress));
        setWaitingForHeadphone(true);
        setState(Disconnected);
        // Keep a retry running even though nothing is connected: a headphone that
        // was already on when the applet started produces no BlueZ event, so
        // without this the wait would last until the user pressed "Retry now".
        if (m_autoConnect && m_autoReconnect && !m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(qBound(2000, m_reconnectDelayMs, 30000));
        }
        return;
    }
    setWaitingForHeadphone(false);
    m_channelValidated = false;
    m_sawBusy = false;
    m_scanGaveUp = false;
    m_busyRetries = 0;
    m_channelIndex = 0;
    m_scanStream.clear();
    clearError();

    if (m_injectedTransport) {
        // The injected transport (development tools, tests) is the only socket
        // there is, but the scan still runs on it - that is what keeps the
        // "a wrong channel accepts and then stays silent" handling under test
        // without hardware.
        if (!m_scanTransport) {
            m_scanTransport = m_transport;
            // the device must not see the scan traffic; the handlers move back in
            // onScanData() once the channel is settled
            m_scanTransport->disconnect(this);
        }
    }

    setState(Connecting);
    Q_EMIT logMessage(moondropTr("Looking for the control channel…"));
    probeNextChannel();
}

void MoondropDevice::probeNextChannel()
{
    // Candidates are tried one at a time.  A MOONDROP headphone serves exactly
    // one control connection, so probing several channels at once does not make
    // the scan faster - the sockets simply take the single slot away from each
    // other (the "wrong" ones get EBUSY and the real one may lose the race).
    // What makes this fast instead is the deadline below: a channel that does not
    // exist is refused in ~35 ms, and one that exists answers immediately.
    if (m_channelValidated || m_scanGaveUp) {
        return;
    }
    if (m_channelIndex >= m_channelCandidates.size()) {
        // Every candidate was tried.  If the device was busy at some point,
        // report that instead of "not found": it means the headphone is there
        // but another program holds its control connection.
        if (m_sawBusy) {
            finishScanBusy();
        } else {
            finishScanNotFound();
        }
        return;
    }

    const int channel = m_channelCandidates.at(m_channelIndex);
    ++m_channelIndex;

    // One socket for the whole scan.  Closing the previous one first matters:
    // an abandoned connect() keeps the kernel's pending RFCOMM request alive,
    // and a new connect() to the same device would be refused with EBUSY until
    // that request is gone.
    Transport *socket = scanTransport();
    if (!m_scanHandlersAttached) {
        m_scanHandlersAttached = true;
        connect(socket, &Transport::connected, this, &MoondropDevice::onScanConnected);
        connect(socket, &Transport::dataReceived, this, &MoondropDevice::onScanData);
        connect(socket, &Transport::errorOccurred, this, &MoondropDevice::onScanError);
    }
    socket->disconnectFromDevice();
    const int timeout = qBound(500, m_probeTimeoutMs, 4000);
    m_scanTimer->start(timeout);
    socket->connectToDevice(m_settingsAddress, channel, timeout);
}

void MoondropDevice::onScanConnected()
{
    if (m_channelValidated || m_scanGaveUp) {
        return;
    }
    // The channel accepted the connection.  That alone proves nothing (a wrong
    // channel can accept and then stay silent), so ask the one question every
    // MOONDROP firmware answers immediately.
    scanTransport()->write(encodeFrame(FeatureBasic, CBasicGetApplicationVersion,
                                       QByteArray(), TypeCommand, VendorMoondrop));
}

void MoondropDevice::onScanData(const QByteArray &data)
{
    if (m_channelValidated || m_scanGaveUp) {
        return;
    }
    // Any well formed GAIA frame may arrive first: some firmwares announce a
    // state change the moment the channel opens.  TCP-like streaming means a
    // frame can be split across reads, so the partial data is kept in the scan's
    // own stream (a fresh one per read would drop the frame and never validate
    // the channel).
    const QList<Frame> frames = m_scanStream.feed(data);
    if (frames.isEmpty()) {
        return;
    }

    m_scanTimer->stop();
    m_channelValidated = true;
    m_scanGaveUp = true;
    Transport *socket = scanTransport();
    const int channel = socket->channel();
    Q_EMIT logMessage(moondropTr("Found the control channel on %1 (channel %2)")
                          .arg(m_settingsAddress)
                          .arg(channel));

    // The scan handlers are dropped before the device ones are attached: the
    // frame that validated the channel is fed to the device below, and keeping
    // both sets of handlers would deliver it twice.
    socket->disconnect(this);
    if (socket != m_transport) {
        // The socket that carried the answer becomes the device transport, so the
        // validated connection is not thrown away and reopened.
        m_transport->disconnect(this);
        m_transport->disconnectFromDevice();
        m_transport->setParent(nullptr);
        m_transport->deleteLater();
        m_transport = socket;
        m_transport->setParent(this);
    }
    connect(m_transport, &Transport::connected, this, &MoondropDevice::onTransportConnected);
    connect(m_transport, &Transport::disconnected, this, &MoondropDevice::onTransportDisconnected);
    connect(m_transport, &Transport::dataReceived, this, &MoondropDevice::onDataReceived);
    connect(m_transport, &Transport::errorOccurred, this, &MoondropDevice::onError);
    m_scanTransport = nullptr;
    m_scanHandlersAttached = false;

    m_settings->setValue(QStringLiteral("device/lastChannel"), channel);
    m_settings->sync();
    m_reconnectDelayMs = 2000;
    m_reconnectAttempts = 0;

    // The channel has proved itself by answering, so this *is* the connected
    // state.  onTransportConnected() is deliberately not used here: it exists for
    // the path where a transport is connected without a scan (injected fake,
    // command line) and has to verify the channel with a queued probe request -
    // doing that here as well would ask the firmware version a second time.
    clearError();
    setState(Connected);
    // The frame that validated the channel is already parsed data (it is the
    // answer to the scan's probe), so feed it before the regular refresh.
    for (const Frame &frame : frames) {
        handleFrame(frame);
    }
    handshake();
}

void MoondropDevice::onScanError(const QString &message)
{
    if (m_channelValidated || m_scanGaveUp) {
        return;
    }
    const int err = scanTransport()->lastErrno();
    const int channel = scanTransport()->channel();
    m_scanTimer->stop();

    if (err == EBUSY) {
        // The headphone serves one control connection at a time, and every
        // channel shares that single slot: EBUSY therefore says "the headphone is
        // up, but the slot is taken", never "this channel is wrong".  Another
        // program may hold it (another widget, the phone app), or it may be our
        // own previous socket whose teardown has not finished - so wait a moment
        // and retry the same channel rather than walking through the list.
        m_sawBusy = true;
        scanTransport()->disconnectFromDevice();
        if (m_busyRetries < 4) {
            ++m_busyRetries;
            Q_EMIT logMessage(moondropTr("The control channel is in use, retrying…"));
            QTimer::singleShot(700, this, [this] {
                if (m_channelValidated || m_scanGaveUp) {
                    return;
                }
                --m_channelIndex; // try the same candidate again
                probeNextChannel();
            });
            return;
        }
        Q_EMIT logMessage(moondropTr("Channel %1: %2").arg(channel).arg(message));
        scanTransport()->disconnectFromDevice();
        QTimer::singleShot(300, this, &MoondropDevice::probeNextChannel);
        return;
    }

    // Not there (ECONNREFUSED/EHOSTDOWN/...): the expected case while scanning.
    if (qEnvironmentVariableIsSet("MOONDROP_DEBUG")) {
        std::fprintf(stderr, "[scan] channel %d: %s\n", channel, qPrintable(message));
    }
    scanTransport()->disconnectFromDevice();
    probeNextChannel();
}

void MoondropDevice::onScanTimeout()
{
    if (m_channelValidated || m_scanGaveUp) {
        return;
    }
    // The channel accepted the connection but never answered: not the GAIA
    // channel.  Move on instead of waiting for the kernel's own timeout.
    const int channel = scanTransport()->channel();
    Q_EMIT logMessage(moondropTr("Channel %1 does not answer, trying the next one…").arg(channel));
    scanTransport()->disconnectFromDevice();
    probeNextChannel();
}

Transport *MoondropDevice::scanTransport()
{
    if (!m_scanTransport) {
        // It has the lifetime of the device: a cancelled connect() needs the
        // socket to stay alive until the kernel has dropped the request.
        m_scanTransport = new RfcommClient(this);
    }
    return m_scanTransport;
}

void MoondropDevice::finishScanBusy()
{
    m_scanGaveUp = true;
    m_scanTimer->stop();
    setState(Disconnected);
    // one literal so the string extractor sees the whole message
    setError(moondropTr("The control channel of %1 is already in use by another program "
                        "(for example another Plasma widget, or the phone app). Disconnect there first - "
                        "the headphone accepts only one connection at a time.")
                 .arg(m_settingsAddress));
    if (m_autoReconnect && m_autoConnect) {
        // try again later; whoever holds the link may let go of it
        m_reconnectAttempts++;
        m_reconnectTimer->start(m_reconnectDelayMs);
        m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, 60000);
    }
}

void MoondropDevice::finishScanNotFound()
{
    m_scanGaveUp = true;
    m_scanTimer->stop();
    setState(Disconnected);
    setError(moondropTr("Could not find a responding GAIA channel on %1. Is the headphone switched on?")
                 .arg(m_settingsAddress));
    if (m_autoReconnect && m_autoConnect) {
        m_reconnectAttempts++;
        m_reconnectTimer->start(m_reconnectDelayMs);
        m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, 60000);
    }
}

void MoondropDevice::disconnectDevice()
{
    m_reconnectTimer->stop();
    m_scanGaveUp = true;
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
    // The scan validates the channel before adopting it; connecting a transport
    // directly (command line, tests) still has to prove that it answered.  The
    // probe request below doubles as that proof either way.
    Request probe;
    probe.feature = FeatureBasic;
    probe.command = CBasicGetApplicationVersion;
    probe.what = QStringLiteral("channel-probe");
    probe.timeoutMs = 700;
    probe.maxAttempts = 2;
    enqueue(probe);
}


void MoondropDevice::onTransportDisconnected()
{
    const bool wasConnected = (m_state == Connected);
    // the information belongs to the headphone that just went away
    clearDeviceInfo();
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
    // Errors from a socket that is still being probed are handled by the probe
    // callbacks (they carry the channel number); this path only covers the
    // adopted channel.
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
        // only reached via the request queue (the scan validates the channel
        // before adopting it), but keep it consistent: this is the first answer
        clearError();
        m_channelValidated = true;
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
        // the device may not have a usable GAIA battery feature at all (the
        // NEKOCAKE answers nothing here) - BlueZ may still know its level
        readBluetoothBattery();
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

void MoondropDevice::readBluetoothBattery()
{
    // Some models (the Bluetrum based NEKOCAKE among them) do not implement the
    // GAIA battery feature at all, but BlueZ reads their level from the standard
    // Battery Service.  Fall back to it so the applet is not left without a
    // percentage.
    if (m_settingsAddress.isEmpty()) {
        return;
    }
    QDBusMessage request =
        QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), bluezDevicePath(),
                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                       QStringLiteral("Get"));
    request << QStringLiteral("org.bluez.Battery1") << QStringLiteral("Percentage");
    const QDBusMessage reply = QDBusConnection::systemBus().call(request, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return;
    }
    const int level = reply.arguments().at(0).value<QDBusVariant>().variant().toInt();
    if (level < 0 || level > 100) {
        return;
    }
    if (m_bluetoothBattery == level) {
        return;
    }
    m_bluetoothBattery = level;
    // Only used while the device itself reports nothing: on models that do have
    // the GAIA battery feature the value read there is the authoritative one.
    if (m_batteryLevel < 0 && m_batteries.isEmpty()) {
        m_batteryLevel = level;
        Q_EMIT batteryChanged();
    }
}

QString MoondropDevice::bluezDevicePath() const
{
    return QStringLiteral("/org/bluez/hci0/dev_")
           + QString(m_settingsAddress).replace(QLatin1Char(':'), QLatin1Char('_'));
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
    if (!hasFeature(FeatureBattery)) {
        // no GAIA battery on this model: BlueZ's Battery1 is the only source
        readBluetoothBattery();
    }
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
