// Checks the output gain label mapping without a headphone attached.
//
//   ./build/cli/preview tests/output-gain.qml /tmp/gain.png 460 400
//
// MOONDROP firmware counts the gain steps downwards (device value 0 = highest),
// so value 0 has to light up "High".  The buttons are also rendered, but the
// offscreen style does not draw their checked state, hence the text summary.
import QtQuick
import QtQuick.Layouts
import "." as Ui

ColumnLayout {
    spacing: 14

    function check(gain, reversed) {
        // index of the button that would be highlighted
        const values = reversed ? [2, 1, 0] : [0, 1, 2]
        const index = values.indexOf(gain)
        const names = ["Low", "Medium", "High"]
        return (index < 0 ? "nothing" : names[index]) + "   (values: " + values.join(",") + ")"
    }

    Repeater {
        model: [
            { title: "device value 0, firmware counts downwards", gain: 0, reversed: true },
            { title: "device value 1", gain: 1, reversed: true },
            { title: "device value 2", gain: 2, reversed: true },
            { title: "device value 0, reversed off", gain: 0, reversed: false },
            { title: "device value 2, reversed off", gain: 2, reversed: false },
            { title: "not reported (-1)", gain: -1, reversed: true }
        ]

        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 3

            Text {
                text: modelData.title
                color: "#cccccc"
            }

            Text {
                text: "  highlighted: " + check(modelData.gain, modelData.reversed)
                color: "#7fd4ff"
                font.bold: true
            }

            Ui.OutputGain {
                Layout.fillWidth: true
                deviceGain: modelData.gain
                reversed: modelData.reversed
                interactive: false
            }
        }
    }
}
