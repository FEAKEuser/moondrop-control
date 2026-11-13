// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

PlasmaComponents3.ScrollView {
    id: page

    // Settings come from the backend as a list, one entry per feature the
    // headphone actually reports, so this page never shows a control the device
    // cannot handle.
    //   { id, label, kind: "bool"|"int", value, min, max }
    function updateValue(id, value) {
        Moondrop.setExtra(id, value)
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: Moondrop.extraSettings.length > 0
            text: i18n("Device settings")
            font.bold: true
        }

        Repeater {
            model: Moondrop.extraSettings

            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 2

                PlasmaComponents3.Switch {
                    Layout.fillWidth: true
                    visible: modelData.kind === "bool"
                    enabled: Moondrop.connected && !Moondrop.busy
                    text: modelData.label
                    checked: modelData.value === true
                    onToggled: page.updateValue(modelData.id, checked)
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: modelData.kind === "int"

                    PlasmaComponents3.Label {
                        Layout.fillWidth: true
                        text: modelData.label
                        font: Kirigami.Theme.smallFont
                        elide: Text.ElideRight
                    }
                    PlasmaComponents3.SpinBox {
                        enabled: Moondrop.connected && !Moondrop.busy
                        from: modelData.min
                        to: modelData.max
                        value: modelData.value
                        onValueModified: page.updateValue(modelData.id, value)
                    }
                }
            }
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: Moondrop.extraSettings.length === 0 && Moondrop.connected
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: i18n("This headphone does not report any of the extra settings (LED, timeouts, wind noise, spatial audio, …).")
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: Moondrop.gestures.length > 0
            text: i18n("Button gestures")
            font.bold: true
        }

        Repeater {
            model: Moondrop.gestures

            ColumnLayout {
                id: gestureBlock
                required property var modelData
                readonly property int gestureId: modelData.id
                Layout.fillWidth: true
                spacing: 1

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    text: gestureBlock.modelData.label
                    opacity: 0.8
                    font: Kirigami.Theme.smallFont
                }

                Repeater {
                    model: gestureBlock.modelData.entries

                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font: Kirigami.Theme.smallFont
                            text: modelData.touchpadLabel + " · " + modelData.contextLabel
                        }
                        PlasmaComponents3.ComboBox {
                            enabled: Moondrop.connected && !Moondrop.busy
                            textRole: "label"
                            valueRole: "id"
                            model: Moondrop.gestureActionOptions
                            currentIndex: {
                                const options = Moondrop.gestureActionOptions
                                for (let i = 0; i < options.length; ++i) {
                                    if (options[i].id === modelData.action) {
                                        return i
                                    }
                                }
                                return -1
                            }
                            onActivated: index => Moondrop.setGestureAction(
                                gestureBlock.gestureId, modelData.touchpad,
                                modelData.context, Moondrop.gestureActionOptions[index].id)
                        }
                    }
                }
            }
        }

        PlasmaComponents3.Button {
            Layout.fillWidth: true
            visible: Moondrop.gestures.length > 0
            icon.name: "edit-undo"
            text: i18n("Reset gestures to default")
            enabled: Moondrop.connected && !Moondrop.busy
            onClicked: Moondrop.resetGestureConfiguration()
        }

        ConnectionHint {}

        Item {
            Layout.fillHeight: true
        }
    }
}
