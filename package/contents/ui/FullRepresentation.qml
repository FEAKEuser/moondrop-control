// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

PlasmaExtras.Representation {
    id: fullRep

    // On the desktop this is the representation shown directly (see main.qml),
    // where the applet is sized by its implicit size; in a popup the Layout
    // hints below apply.
    implicitWidth: Kirigami.Units.gridUnit * 23
    implicitHeight: Kirigami.Units.gridUnit * 28
    Layout.minimumWidth: Kirigami.Units.gridUnit * 22
    Layout.minimumHeight: Kirigami.Units.gridUnit * 24
    Layout.preferredWidth: implicitWidth
    Layout.preferredHeight: implicitHeight

    header: PlasmaExtras.PlasmoidHeading {
        RowLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "audio-headphones"
                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: Moondrop.model.length > 0 ? Moondrop.model : i18n("MOONDROP headphone")
                    font.bold: true
                }
                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                    text: Moondrop.statusText + (Moondrop.firmwareVersion.length > 0 ? " · " + Moondrop.firmwareVersion : "")
                }
            }

            PlasmaComponents3.BusyIndicator {
                visible: Moondrop.busy
                running: visible
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }

            PlasmaComponents3.Label {
                visible: root.waitingForDevice && !Moondrop.busy
                text: i18n("waiting for the headphone…")
                opacity: 0.7
                font: Kirigami.Theme.smallFont
            }

            PlasmaComponents3.Button {
                text: Moondrop.connected ? i18n("Disconnect")
                                         : (root.waitingForDevice ? i18n("Retry now") : i18n("Connect"))
                icon.name: Moondrop.connected ? "network-disconnect" : "network-connect"
                // always clickable: pressing Connect while something is stuck
                // restarts cleanly instead of leaving the user without a way out
                onClicked: Moondrop.connected ? Moondrop.disconnectDevice() : Moondrop.connectDevice()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.TabBar {
            id: tabBar
            Layout.fillWidth: true

            PlasmaComponents3.TabButton {
                text: i18n("Noise cancelling")
                icon.name: "audio-volume-muted"
            }
            PlasmaComponents3.TabButton {
                text: i18n("Tuning")
                icon.name: "audio-equalizer"
            }
            PlasmaComponents3.TabButton {
                text: i18n("Sound")
                icon.name: "audio-card"
            }
            PlasmaComponents3.TabButton {
                text: i18n("Info")
                icon.name: "help-about"
            }
        }

        StackLayout {
            currentIndex: tabBar.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            AncPage {}
            EqPage {}
            SoundPage {}
            InfoPage {}
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: Moondrop.lastError.length > 0
            text: Moondrop.lastError
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.negativeTextColor
            font: Kirigami.Theme.smallFont
        }
    }
}
