// SPDX-License-Identifier: GPL-3.0-or-later
// High level MOONDROP headphone controller (GAIA v3/v4 over RFCOMM).
#pragma once

#include "deviceprofile.h"
#include "gaia.h"
#include "transport.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QTimer;

namespace Moondrop {
class BlueZWatcher;
}

namespace Moondrop {

// Which command family the device uses for noise cancelling.
enum AncPath {
    AncPathUnknown = -1,
    AncPathAudioCuration = 8, // feature 8 (EDGE, Golden Ages 2, Space Travel 2, ...)
    AncPathAncV1 = 2, // feature 2 (older TWS)
    AncPathAncV2 = 32, // feature 32 (newer TWS)
};

// UI noise cancelling modes (device independent)
enum AncMode {
    AncOff = 0,
    AncNoiseCancelling = 1,
    AncTransparency = 2,
    AncWind = 3,
    AncAdaptive = 4,
    AncLive = 5,
};

// One 5 band parametric EQ band (MOONDROP "user set configuration").
struct EqBand
{
    int frequency = 1000; // Hz
    double q = 1.0;
    double gain = 0.0; // dB
    int type = 0; // 0 = peaking
};

// Decode / encode the 39 byte user EQ payload of EDGE style firmware.
QList<EqBand> decodeUserEq(const QByteArray &payload);
QByteArray encodeUserEq(const QList<EqBand> &bands);

class MoondropDevice : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // true once a full state refresh has finished (the widget can then trust the
    // values it displays)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // The headphone is not reachable *at the Bluetooth level*: the user has to
    // connect it in the system settings (or switch it on) before we can do
    // anything.  Retrying the control channel would be pointless until then.
    Q_PROPERTY(bool waitingForHeadphone READ waitingForHeadphone NOTIFY waitingChanged)
    // a curve the headphone reported earlier, so an accidental change can be undone
    Q_PROPERTY(bool hasDeviceEqBackup READ hasDeviceEqBackup NOTIFY eqChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY settingsChanged)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY settingsChanged)

    Q_PROPERTY(QString model READ model NOTIFY infoChanged)
    Q_PROPERTY(QString profileId READ profileId NOTIFY profileChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY profileChanged)
    Q_PROPERTY(bool profileVerified READ profileVerified NOTIFY profileChanged)
    Q_PROPERTY(QString profileNotes READ profileNotes NOTIFY profileChanged)
    Q_PROPERTY(QString profileOverride READ profileOverride NOTIFY profileChanged)
    // [{ id, label, verified }] for the settings page
    Q_PROPERTY(QVariantList availableProfiles READ availableProfiles NOTIFY profileChanged)
    Q_PROPERTY(bool buds READ buds NOTIFY profileChanged)
    Q_PROPERTY(bool gainOrderKnown READ gainOrderKnown NOTIFY profileChanged)
    Q_PROPERTY(bool gainDescending READ gainDescending NOTIFY profileChanged)
    // number of PEQ bands the device supports (0 = no parametric EQ)
    Q_PROPERTY(int peqBandCount READ peqBandCount NOTIFY eqChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY infoChanged)
    Q_PROPERTY(QString serialNumber READ serialNumber NOTIFY infoChanged)
    Q_PROPERTY(QString gaiaVersion READ gaiaVersion NOTIFY infoChanged)
    Q_PROPERTY(QString hostAddress READ hostAddress NOTIFY infoChanged)
    Q_PROPERTY(QVariantList features READ features NOTIFY infoChanged)

    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryChanged)
    Q_PROPERTY(QVariantList batteries READ batteries NOTIFY batteryChanged)

    Q_PROPERTY(int ancPath READ ancPath NOTIFY capabilitiesChanged)
    Q_PROPERTY(int ancMode READ ancMode NOTIFY ancModeChanged)
    Q_PROPERTY(bool ancSupported READ ancSupported NOTIFY capabilitiesChanged)

    Q_PROPERTY(bool eqSupported READ eqSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(int eqBandsCount READ eqBandsCount NOTIFY eqChanged)
    Q_PROPERTY(QVariantList presetIds READ presetIds NOTIFY eqChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetNamesChanged)
    Q_PROPERTY(int currentPreset READ currentPreset NOTIFY eqChanged)
    Q_PROPERTY(QVariantList bands READ bands NOTIFY eqChanged)

    Q_PROPERTY(bool ldacSupported READ ldacSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool ldacEnabled READ ldacEnabled NOTIFY codecChanged)
    Q_PROPERTY(bool lc3Supported READ lc3Supported NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool lc3Enabled READ lc3Enabled NOTIFY codecChanged)
    Q_PROPERTY(bool lhdcSupported READ lhdcSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool lhdcEnabled READ lhdcEnabled NOTIFY codecChanged)
    Q_PROPERTY(bool dacGainSupported READ dacGainSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(int dacGain READ dacGain NOTIFY codecChanged)
    Q_PROPERTY(bool multipointSupported READ multipointSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool multipointEnabled READ multipointEnabled NOTIFY codecChanged)

    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QVariantList pendingBands READ pendingBands NOTIFY eqChanged)

public:
    enum State { Disconnected, Connecting, Connected, NotResponding };
    Q_ENUM(State)

    // The transport defaults to a Bluetooth RFCOMM client; the development tools
    // pass a fake one to work without hardware.
    explicit MoondropDevice(QObject *parent = nullptr, Transport *transport = nullptr);
    ~MoondropDevice() override;

    State state() const { return m_state; }
    // the headphone answers and the link is up
    bool connected() const { return m_state == Connected && m_transport != nullptr && m_transport->isConnected(); }
    // true while connecting, or while a command is in flight or queued
    bool busy() const
    {
        return m_state == Connecting || m_hasInFlight || !m_queue.isEmpty();
    }
    bool ready() const { return m_ready; }
    bool waitingForHeadphone() const { return m_waitingForHeadphone; }
    bool hasDeviceEqBackup() const { return !m_deviceEqBackup.isEmpty(); }
    QString statusText() const;

    QString address() const { return m_settingsAddress; }
    void setAddress(const QString &address);
    int channel() const { return m_settingsChannel; }
    void setChannel(int channel);
    bool autoConnect() const { return m_autoConnect; }
    void setAutoConnect(bool enabled);
    bool autoReconnect() const { return m_autoReconnect; }
    void setAutoReconnect(bool enabled);

    QString model() const { return m_model; }
    QString profileId() const { return m_profile.id; }
    QString profileName() const;
    bool profileVerified() const { return m_profile.verified; }
    QString profileNotes() const;
    QString profileOverride() const;
    QVariantList availableProfiles() const;
    bool buds() const { return m_profile.isBuds; }
    bool gainOrderKnown() const { return m_profile.gainOrder != GainOrder::Unknown; }
    bool gainDescending() const { return m_profile.gainOrder == GainOrder::Descending; }
    int peqBandCount() const { return m_peqBandCount; }
    QString firmwareVersion() const { return m_firmware; }
    QString serialNumber() const { return m_serial; }
    QString gaiaVersion() const { return m_gaiaVersion; }
    QString hostAddress() const { return m_hostAddress; }
    QVariantList features() const { return m_features; }

    int batteryLevel() const { return m_batteryLevel; }
    QVariantList batteries() const { return m_batteries; }

    int ancPath() const { return m_ancPath; }
    int ancMode() const { return m_ancMode; }
    bool ancSupported() const { return m_ancPath != AncPathUnknown; }

    bool eqSupported() const { return m_eqSupported; }
    int eqBandsCount() const { return m_bands.size(); }
    QVariantList presetIds() const { return m_presetIds; }
    QStringList presetNames() const { return m_presetNames; }
    int currentPreset() const { return m_currentPreset; }
    QVariantList bands() const { return m_bands; }
    // bands that were last written and are still being verified
    QVariantList pendingBands() const { return m_eqWriteTarget; }

    bool ldacSupported() const { return m_ldacSupported; }
    bool ldacEnabled() const { return m_ldacEnabled; }
    bool lc3Supported() const { return m_lc3Supported; }
    bool lc3Enabled() const { return m_lc3Enabled; }
    bool lhdcSupported() const { return m_lhdcSupported; }
    bool lhdcEnabled() const { return m_lhdcEnabled; }
    bool dacGainSupported() const { return m_dacGainSupported; }
    int dacGain() const { return m_dacGain; }
    bool multipointSupported() const { return m_multipointSupported; }
    bool multipointEnabled() const { return m_multipointEnabled; }

    QString lastError() const { return m_lastError; }

    // ---- QML API -----------------------------------------------------------
    Q_INVOKABLE void connectDevice();
    Q_INVOKABLE void connectTo(const QString &address, int channel = -1);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void refresh();
    // Apply a profile by hand (the settings page offers this when auto detection
    // picks the wrong one).
    Q_INVOKABLE void setProfileOverride(const QString &profileId);
    Q_INVOKABLE void setAncMode(int mode);
    Q_INVOKABLE void selectPreset(int presetId);
    Q_INVOKABLE void applyUserEq(const QVariantList &bands, bool selectCustom = true);
    Q_INVOKABLE void requestUserEq();
    // Put back the curve the headphone reported before it was changed.
    Q_INVOKABLE void restoreDeviceEq();
    Q_INVOKABLE void setLdacEnabled(bool enabled);
    Q_INVOKABLE void setLc3Enabled(bool enabled);
    Q_INVOKABLE void setLhdcEnabled(bool enabled);
    Q_INVOKABLE void setDacGain(int gain);
    Q_INVOKABLE void setMultipointEnabled(bool enabled);
    Q_INVOKABLE QString presetName(int presetId) const;
    // Escape hatch for reverse engineering: send an arbitrary GAIA command,
    // payload given as hex string, e.g. sendRaw(5, 5, "0004").
    Q_INVOKABLE void sendRaw(int feature, int command, const QString &payloadHex = QString(),
                             bool expectReply = true);
    Q_INVOKABLE void clearError();

    // The applet connects by itself; tools that manage the connection
    // themselves (moondrop-cli) switch that off right after construction.
    void disableStartupAutoConnect();

Q_SIGNALS:
    void stateChanged();
    void busyChanged();
    void readyChanged();
    void waitingChanged();
    void settingsChanged();
    void infoChanged();
    void profileChanged();
    void batteryChanged();
    void capabilitiesChanged();
    void ancModeChanged();
    void eqChanged();
    void codecChanged();
    void presetNamesChanged();
    void lastErrorChanged();
    void logMessage(const QString &message);
    void commandFinished(const QString &what, bool ok);

private Q_SLOTS:
    void onTransportConnected();
    void onTransportDisconnected();
    void onDataReceived(const QByteArray &data);
    void onError(const QString &message);
    void onRequestTick();
    void onStartupAutoConnect();
    void onDeviceAppeared(const QString &address);
    void onDeviceVanished(const QString &address);
    void onReconnectTimer();

private:
    // The firmware handles one command at a time: requests are queued and paced,
    // otherwise it silently drops most of a burst.
    struct Request
    {
        quint16 feature = 0;
        quint8 command = 0;
        QByteArray payload;
        QString what;
        bool expectReply = true;
        int attempts = 0;
        int maxAttempts = 2;
        int timeoutMs = 900;
        qint64 sentAt = 0;
        bool sent = false;
        std::function<void()> onTimeout;
        std::function<void()> onDone;
        bool barrier = false; // never sent; marks "everything before me is done"
        bool silent = false; // occupy the queue without writing (settling time)
    };

    void setState(State state);
    void setError(const QString &message);
    void loadSettings();
    void saveSettings();
    void send(quint16 feature, quint8 command, const QByteArray &payload = QByteArray(), bool expectReply = true,
              const QString &what = QString());
    void enqueue(const Request &request);
    void prepareUserAction();
    // write a frame without queueing it (used for commands the device does not
    // acknowledge, where the caller manages the timing itself)
    void writeNow(quint16 feature, quint8 command, const QByteArray &payload = QByteArray());
    void enqueuePresetSelect(int presetId);
    void pumpQueue();
    void advanceChannel();
    void handleFrame(const Frame &frame);
    void handleResponse(const Frame &frame);
    void handleNotification(const Frame &frame);
    // Rate limit for "a notification arrived, so re-read that feature" reactions.
    // Reading a feature can itself produce a notification, which would otherwise
    // ping-pong forever and keep the device (and the UI) permanently busy.
    bool notificationRefreshAllowed(quint16 feature);
    void handshake();
    void refreshDetails();
    void probeOtherAncPaths();
    bool hasFeature(quint16 feature) const;
    void parseBattery(const QByteArray &payload);
    void applyProfile(const QString &modelName);
    void setWaitingForHeadphone(bool waiting);
    void rememberDeviceEq(const QVariantList &bands);
    void parseAncMode(const QByteArray &payload);
    void parsePresets(const QByteArray &payload);
    void parseSelectedPreset(const QByteArray &payload);
    void parseBandCount(const QByteArray &payload);
    void parseUserEq(const QByteArray &payload);
    void parseCodec(quint16 feature, quint8 command, const QByteArray &payload);
    void parseDacGain(const QByteArray &payload);
    void parseMultipoint(const QByteArray &payload);
    void parseSupportedFeatures(const QByteArray &payload);
    void updatePresetNames();
    void tryNextChannel();

    Transport *m_transport = nullptr;
    QSettings *m_settings = nullptr;
    QTimer *m_requestTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_probeTimer = nullptr;

    State m_state = Disconnected;
    QString m_lastError;

    // settings
    QString m_settingsAddress;
    int m_settingsChannel = 0; // 0 = auto
    bool m_autoConnect = false;
    bool m_autoReconnect = true;

    // info
    QString m_model;
    DeviceProfile m_profile;
    QString m_profileOverride;
    int m_peqBandCount = 0;
    QString m_firmware;
    QString m_serial;
    QString m_gaiaVersion;
    QString m_hostAddress;
    QVariantList m_features;

    // battery
    int m_batteryLevel = -1;
    QVariantList m_batteries;

    // ANC
    int m_ancPath = AncPathUnknown;
    int m_ancMode = -1;

    // EQ
    bool m_eqSupported = false;
    QVariantList m_presetIds;
    QStringList m_presetNames;
    int m_currentPreset = -1;
    QVariantList m_bands;
    QVariantList m_eqWriteTarget;
    int m_eqWriteAttempts = 0;
    void writeEqPayload(const QVariantList &bands);
    // snapshot == false for a restore: the restore point must survive it
    void applyUserEqInternal(const QVariantList &bands, bool selectCustom, bool snapshot);
    static bool bandsMatch(const QVariantList &a, const QVariantList &b);

    // codec / misc
    bool m_ldacSupported = false;
    bool m_ldacEnabled = false;
    bool m_lc3Supported = false;
    bool m_lc3Enabled = false;
    bool m_lhdcSupported = false;
    bool m_lhdcEnabled = false;
    bool m_dacGainSupported = false;
    int m_dacGain = -1;
    bool m_multipointSupported = false;
    bool m_multipointEnabled = false;

    Stream m_stream;
    QQueue<Request> m_queue;
    Request m_inFlight;
    bool m_hasInFlight = false;
    QSet<quint16> m_supportedFeatures;
    QHash<quint16, qint64> m_lastNotificationRefresh;
    BlueZWatcher *m_bluetoothWatcher = nullptr;
    bool m_startupAutoConnect = true;
    // grows while the headphone cannot be reached, so a switched off device does
    // not get hammered
    int m_reconnectDelayMs = 2000;
    int m_reconnectAttempts = 0;
    bool m_featuresKnown = false;
    QList<int> m_channelCandidates;
    int m_channelIndex = 0;
    bool m_channelValidated = false;
    bool m_ready = false;
    bool m_waitingForHeadphone = false;
    // last curve read from the headphone that we did not write ourselves
    QVariantList m_deviceEqBackup;
    int m_busyRetries = 0;
};

} // namespace Moondrop
