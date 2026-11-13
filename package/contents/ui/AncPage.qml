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
        { mode: 0, label: i18n("Off"), icon: "audio-volume-muted-symbolic" },
        { mode: 1, label: i18n("Noise cancelling"), icon: "noisereduction-symbolic" },
        { mode: 2, label: i18n("Transparency"), icon: "audio-volume-high-symbolic" }
    ]

    // ANC order values from the app's ACOrders enum (PROTOCOL 13.5)
    readonly property var switchOrders: [
        { value: 0, label: i18n("On → Off → Transparency") },
        { value: 1, label: i18n("Off → On → Transparency") },
        { value: 2, label: i18n("Transparency → On → Off") },
        { value: 3, label: i18n("On → Transparency → Off") },
        { value: 4, label: i18n("Off → Transparency → On") },
        { value: 5, label: i18n("Transparency → Off → On") }
    ]

    function applySwitch(changes) {
        const config = Moondrop.ancSwitchConfig
        Moondrop.setAncSwitchConfig(
            changes.state !== undefined ? changes.state : config.state === true,
            changes.ancOn !== undefined ? changes.ancOn : config.ancOn === true,
            changes.ancOff !== undefined ? changes.ancOff : config.ancOff === true,
            changes.transparency !== undefined ? changes.transparency : config.transparency === true,
            changes.order !== undefined ? changes.order : config.order)
    }

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
                    // equal thirds: without the cap, the width follows the
                    // label ("Noise cancelling" is wider than "Off")
                    Layout.maximumWidth: (page.availableWidth - 2 * Kirigami.Units.smallSpacing) / 3
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

        // Which modes the button on the headphone may cycle through (GAIA
        // AudioCuration/ANC V2 command 41/42).
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: Moondrop.ancSwitchConfig.present === true
            text: i18n("Switchable modes")
            font.bold: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            visible: Moondrop.ancSwitchConfig.present === true

            PlasmaComponents3.Switch {
                Layout.fillWidth: true
                enabled: Moondrop.connected && !Moondrop.busy
                text: i18n("Cycle modes with the headphone button")
                checked: Moondrop.ancSwitchConfig.state === true
                onToggled: page.applySwitch({ state: checked })
                PlasmaComponents3.ToolTip {
                    // measured on EDGE: the firmware ignores writing 0 here
                    text: i18n("Some firmware cannot turn this off; the switch returns to the device's value.")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                PlasmaComponents3.CheckBox {
                    Layout.fillWidth: true
                    enabled: Moondrop.connected && !Moondrop.busy
                    text: i18n("Noise cancelling")
                    checked: Moondrop.ancSwitchConfig.ancOn === true
                    onToggled: page.applySwitch({ ancOn: checked })
                }
                PlasmaComponents3.CheckBox {
                    Layout.fillWidth: true
                    enabled: Moondrop.connected && !Moondrop.busy
                    text: i18n("Transparency")
                    checked: Moondrop.ancSwitchConfig.transparency === true
                    onToggled: page.applySwitch({ transparency: checked })
                }
                PlasmaComponents3.CheckBox {
                    Layout.fillWidth: true
                    enabled: Moondrop.connected && !Moondrop.busy
                    text: i18n("Off")
                    checked: Moondrop.ancSwitchConfig.ancOff === true
                    onToggled: page.applySwitch({ ancOff: checked })
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                PlasmaComponents3.Label {
                    text: i18n("Order")
                }
                PlasmaComponents3.ComboBox {
                    Layout.fillWidth: true
                    enabled: Moondrop.connected && !Moondrop.busy
                    textRole: "label"
                    valueRole: "value"
                    model: page.switchOrders
                    currentIndex: {
                        for (let i = 0; i < model.length; ++i) {
                            if (model[i].value === Moondrop.ancSwitchConfig.order) {
                                return i
                            }
                        }
                        return 0
                    }
                    onActivated: index => page.applySwitch({ order: model[index].value })
                }
            }
        }

        ConnectionHint {
            Layout.topMargin: Kirigami.Units.smallSpacing
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
