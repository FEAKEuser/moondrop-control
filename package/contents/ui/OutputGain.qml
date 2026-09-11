// SPDX-License-Identifier: MIT
// The three output gain steps.
//
// MOONDROP firmware counts the steps downwards: device value 0 is the *highest*
// gain, 2 the lowest.  `reversed` (a widget setting) swaps the mapping for models
// that number them the other way round.
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

RowLayout {
    id: root

    // raw value as reported by the headphone (0, 1 or 2)
    property int deviceGain: -1
    // true when the firmware counts downwards (MOONDROP default)
    property bool reversed: true
    property bool interactive: true

    signal gainSelected(int gain)

    // index 0 = lowest gain
    readonly property var deviceValues: reversed ? [2, 1, 0] : [0, 1, 2]
    readonly property var labels: [i18n("Low"), i18n("Medium"), i18n("High")]

    spacing: Kirigami.Units.smallSpacing

    Repeater {
        model: root.labels

        PlasmaComponents3.Button {
            required property int index
            required property string modelData

            Layout.fillWidth: true
            checkable: true
            checked: root.deviceGain === root.deviceValues[index]
            enabled: root.interactive
            text: modelData
            onClicked: root.gainSelected(root.deviceValues[index])
        }
    }
}
