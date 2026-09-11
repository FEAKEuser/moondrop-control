// SPDX-License-Identifier: MIT
// One line explaining why a page has no data yet.
//
// Telling the user "not connected" while the applet is busy connecting (or while
// it is waiting for the headphone to show up in Bluetooth at all) is misleading,
// so the message depends on what is actually going on.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

import org.moondrop.backend 1.0

PlasmaComponents3.Label {
    id: hint

    property bool wantsConnection: true

    Layout.fillWidth: true
    visible: text.length > 0
    wrapMode: Text.WordWrap
    opacity: 0.75
    font: Kirigami.Theme.smallFont

    text: {
        if (Moondrop.connected) {
            return ""
        }
        if (Moondrop.busy) {
            return i18n("Connecting to the headphone…")
        }
        if (Moondrop.waitingForHeadphone) {
            return i18n("Waiting for the headphone: connect it in the system Bluetooth settings, or switch it on.")
        }
        if (Moondrop.autoConnect && Moondrop.address.length > 0) {
            return i18n("Not reachable right now; the applet keeps trying in the background.")
        }
        return hint.wantsConnection
               ? i18n("Not connected. Use the Connect button above, or pick your headphone in the widget settings.")
               : i18n("Not connected.")
    }
}
