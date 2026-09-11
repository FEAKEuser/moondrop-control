// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

ColumnLayout {
    id: configPage

    DeviceDiscovery {
        id: discovery
        Component.onCompleted: refresh()
    }

    Component.onCompleted: {
        // make sure the current address is represented in the list
        discovery.refresh()
    }

    property int currentIndex: {
        for (let i = 0; i < discovery.devices.length; ++i) {
            if (discovery.devices[i].address === Moondrop.address) {
                return i
            }
        }
        return -1
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        text: i18n("Headphone")
        font.bold: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.ComboBox {
            id: deviceBox
            Layout.fillWidth: true
            model: discovery.devices
            enabled: discovery.devices.length > 0
            textRole: "label"
            valueRole: "address"
            currentIndex: configPage.currentIndex
            onActivated: {
                const device = discovery.devices[index]
                if (device) {
                    Moondrop.setAddress(device.address)
                    Moondrop.setChannel(0)
                }
            }
            displayText: currentIndex >= 0 && discovery.devices[currentIndex]
                         ? discovery.devices[currentIndex].name + "  (" + discovery.devices[currentIndex].address + ")"
                         : i18n("No Bluetooth device found")
            delegate: PlasmaComponents3.ItemDelegate {
                required property var modelData
                required property int index
                width: deviceBox.width
                text: modelData.name + "  (" + modelData.address + ")"
                icon.name: modelData.connected ? "network-connect" : (modelData.paired ? "network-wireless" : "network-wireless-disconnected")
                highlighted: deviceBox.highlightedIndex === index
                onClicked: {
                    deviceBox.currentIndex = index
                    deviceBox.activated(index)
                    deviceBox.popup.close()
                }
            }
        }

        PlasmaComponents3.Button {
            icon.name: "view-refresh"
            enabled: !discovery.busy
            onClicked: discovery.refresh()
            PlasmaComponents3.ToolTip {
                text: i18n("Re-read the device list from BlueZ")
            }
        }
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        visible: discovery.error.length > 0
        text: discovery.error
        color: Kirigami.Theme.negativeTextColor
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        text: i18n("Only paired devices can be controlled. Pair your headphone in the system Bluetooth settings first.")
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        text: i18n("RFCOMM channel")
        font.bold: true
    }

    RowLayout {
        Layout.fillWidth: true
        PlasmaComponents3.SpinBox {
            id: channelBox
            from: 0
            to: 30
            value: Moondrop.channel
            onValueModified: Moondrop.setChannel(value)
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            font: Kirigami.Theme.smallFont
            text: i18n("0 = detect automatically (recommended). MOONDROP EDGE uses channel 1, Space Travel uses 16.")
        }
    }

    PlasmaComponents3.Switch {
        Layout.fillWidth: true
        text: i18n("Connect automatically (and keep trying until the headphone answers)")
        checked: Moondrop.autoConnect
        onToggled: Moondrop.setAutoConnect(checked)
    }

    PlasmaComponents3.Switch {
        Layout.fillWidth: true
        text: i18n("Reconnect automatically after the connection drops")
        checked: Moondrop.autoReconnect
        onToggled: Moondrop.setAutoReconnect(checked)
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.gridUnit * 2
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        text: i18n("The widget connects by itself when Plasma starts, and again whenever the headphone appears. The retry delay grows up to one minute while the headphone is switched off.")
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        text: i18n("Device profile")
        font.bold: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.ComboBox {
            id: profileBox
            Layout.fillWidth: true
            textRole: "label"
            valueRole: "id"
            model: Moondrop.availableProfiles
            currentIndex: {
                for (let i = 0; i < model.length; ++i) {
                    if (model[i].id === Moondrop.profileOverride) {
                        return i
                    }
                }
                return 0
            }
            displayText: currentIndex >= 0 ? model[currentIndex].label : i18n("Detect automatically")
            onActivated: index => Moondrop.setProfileOverride(model[index].id)
            Component.onCompleted: model = Moondrop.availableProfiles
        }
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.gridUnit * 2
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        text: i18n("Detected: %1. Only change this if a feature is missing or labelled wrongly - some models report their name late, so wait until the connection is up.", Moondrop.profileName)
    }

    PlasmaComponents3.Switch {
        Layout.fillWidth: true
        text: i18n("Show the battery level in the panel")
        checked: Plasmoid.configuration.showBattery
        onToggled: Plasmoid.configuration.showBattery = checked
    }

    PlasmaComponents3.Switch {
        Layout.fillWidth: true
        text: i18n("Reverse the output gain levels (0 = highest)")
        checked: Plasmoid.configuration.gainReversed
        onToggled: Plasmoid.configuration.gainReversed = checked
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.gridUnit * 2
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        text: i18n("MOONDROP firmware counts the three gain steps downwards, so “Low” is device value 2. Turn this off if the labels look swapped on your model.")
    }

    Item {
        Layout.fillHeight: true
    }
}
