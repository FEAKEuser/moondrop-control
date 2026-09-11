// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

PlasmaComponents3.ScrollView {
    id: page

    // MOONDROP firmware uses bit style codes for the AudioCuration family
    readonly property var modes: [
        { mode: 0, label: i18n("Off"), icon: "audio-volume-muted" },
        { mode: 1, label: i18n("Noise cancelling"), icon: "audio-volume-low" },
        { mode: 2, label: i18n("Transparency"), icon: "audio-volume-high" }
    ]

    ColumnLayout {
        width: page.availableWidth
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            text: i18n("Noise cancelling mode")
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: page.modes

                PlasmaComponents3.Button {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 4
                    checkable: true
                    checked: Moondrop.ancMode === modelData.mode
                    enabled: Moondrop.connected && Moondrop.ancSupported && !Moondrop.busy
                    text: modelData.label
                    icon.name: modelData.icon
                    display: PlasmaComponents3.AbstractButton.TextUnderIcon
                    onClicked: Moondrop.setAncMode(modelData.mode)
                }
            }
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: Moondrop.ancSupported && Moondrop.connected
            opacity: 0.7
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            text: i18n("Current mode: %1. Switching takes a moment on the headphone side.",
                       Moondrop.ancMode === 0 ? i18n("Off")
                       : Moondrop.ancMode === 1 ? i18n("Noise cancelling")
                       : Moondrop.ancMode === 2 ? i18n("Transparency")
                       : Moondrop.ancMode === 3 ? i18n("Wind noise reduction")
                       : i18n("unknown"))
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: Moondrop.connected && !Moondrop.ancSupported
            wrapMode: Text.WordWrap
            opacity: 0.8
            text: i18n("This headphone does not report a noise cancelling interface. Press “Refresh” on the info page and try again.")
        }

        ConnectionHint {
            Layout.topMargin: Kirigami.Units.smallSpacing
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
