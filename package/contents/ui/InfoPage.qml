// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

PlasmaComponents3.ScrollView {
    id: page

    function featureNames() {
        const names = []
        const list = Moondrop.features
        for (let i = 0; i < list.length; ++i) {
            names.push(list[i].name)
        }
        return names.join(", ")
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            text: i18n("Battery")
            font.bold: true
        }

        Repeater {
            model: Moondrop.batteries

            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 0

                PlasmaComponents3.Label {
                    text: {
                        const names = { 0: i18n("Headphone"), 1: i18n("Left"), 2: i18n("Right"), 3: i18n("Case") }
                        const name = names[modelData.id] !== undefined ? names[modelData.id] : i18n("Battery %1", modelData.id)
                        return name + " — " + modelData.level + " %"
                    }
                }
                PlasmaComponents3.ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: modelData.level
                }
            }
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: Moondrop.batteries.length === 0
            opacity: 0.7
            text: i18n("No battery information available.")
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            text: i18n("Device")
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2

            PlasmaComponents3.Label { text: i18n("Model"); opacity: 0.7 }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: Moondrop.model.length > 0 ? Moondrop.model : "—"
            }

            PlasmaComponents3.Label { text: i18n("Profile"); opacity: 0.7 }
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    text: Moondrop.profileName
                    elide: Text.ElideRight
                }
                PlasmaComponents3.Label {
                    visible: Moondrop.profileVerified
                    text: i18n("verified")
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                }
            }

            PlasmaComponents3.Label { text: i18n("Firmware"); opacity: 0.7 }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: Moondrop.firmwareVersion.length > 0 ? Moondrop.firmwareVersion : "—"
            }

            PlasmaComponents3.Label { text: i18n("GAIA version"); opacity: 0.7 }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: Moondrop.gaiaVersion.length > 0 ? Moondrop.gaiaVersion : "—"
            }

            PlasmaComponents3.Label { text: i18n("Serial"); opacity: 0.7 }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: Moondrop.serialNumber.length > 0 ? Moondrop.serialNumber : "—"
            }

            PlasmaComponents3.Label { text: i18n("Address"); opacity: 0.7 }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: Moondrop.address.length > 0 ? Moondrop.address : "—"
            }

            PlasmaComponents3.Label { text: i18n("Profile source"); opacity: 0.7 }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                visible: Moondrop.profileNotes.length > 0
                wrapMode: Text.WordWrap
                opacity: 0.8
                font: Kirigami.Theme.smallFont
                text: Moondrop.profileNotes
            }

            PlasmaComponents3.Label { text: i18n("Linked host"); opacity: 0.7 }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: Moondrop.hostAddress.length > 0 ? Moondrop.hostAddress : "—"
            }

            PlasmaComponents3.Label { text: i18n("RFCOMM channel"); opacity: 0.7 }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: Moondrop.channel > 0 ? Moondrop.channel : i18n("automatic")
            }
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: Moondrop.features.length > 0
            text: i18n("Supported features")
            font.bold: true
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            font: Kirigami.Theme.smallFont
            text: page.featureNames()
        }

        PlasmaComponents3.Button {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            icon.name: "view-refresh"
            text: i18n("Refresh")
            enabled: Moondrop.connected && !Moondrop.busy
            onClicked: Moondrop.refresh()
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
