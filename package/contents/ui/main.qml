// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

PlasmoidItem {
    id: root

    readonly property bool connected: Moondrop.connected
    readonly property int battery: Moondrop.batteryLevel
    // true when the applet lives on the desktop instead of in a panel
    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar
    // the backend knows the headphone is offline and waits for Bluetooth to
    // report it again
    readonly property bool waitingForDevice: Moondrop.waitingForHeadphone

    switchWidth: Kirigami.Units.gridUnit * 14
    switchHeight: Kirigami.Units.gridUnit * 18

    // On the desktop show the controls right away instead of an icon that has to
    // be clicked to open a popup; in a panel the compact icon stays compact.
    preferredRepresentation: onDesktop ? fullRepresentation : null

    compactRepresentation: CompactRepresentation {
        batteryLevel: root.battery
        showBattery: Plasmoid.configuration.showBattery
    }

    fullRepresentation: FullRepresentation {
    }

    Plasmoid.title: i18n("Moondrop Control")
    toolTipMainText: i18n("Moondrop Control")
    toolTipSubText: {
        let text = Moondrop.model.length > 0 ? Moondrop.model : i18n("MOONDROP headphone")
        text += "\n" + Moondrop.statusText
        // show every battery the headphone reports (earbuds have two, plus a case)
        const batteries = Moondrop.batteries
        for (let i = 0; i < batteries.length; ++i) {
            if (batteries[i].valid === false) {
                continue
            }
            text += "\n" + batteryName(batteries[i].id) + ": " + batteries[i].level + " %"
        }
        if (batteries.length === 0 && root.connected && root.battery >= 0) {
            text += "\n" + i18n("Battery: %1 %", root.battery)
        }
        return text
    }

    function batteryName(id) {
        switch (id) {
        case 0: return i18n("Headphone")
        case 1: return i18n("Left")
        case 2: return i18n("Right")
        case 3: return i18n("Case")
        default: return i18n("Battery %1", id)
        }
    }

    // Connecting happens in the backend (see MoondropDevice::onStartupAutoConnect):
    // the applet does not have to ask for it, and the state survives a widget
    // reload.
}
