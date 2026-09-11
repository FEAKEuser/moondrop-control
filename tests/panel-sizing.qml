// Checks the panel sizing rules of CompactRepresentation:
//
//   ./build/cli/preview tests/panel-sizing.qml /tmp/sizing.png 460 300
//
// A panel applet must stay inside the space the panel gives it (otherwise it
// overlaps its neighbours), and it must not grow when the icon is large.  The
// test instantiates the indicator at several panel thicknesses and prints the
// implicit size it asks for.
import QtQuick
import QtQuick.Layouts
import "." as Ui

ColumnLayout {
    spacing: 6

    function report(thickness, width, showBattery) {
        return "panel " + thickness + " px -> asks for " + width.toFixed(0)
               + " x " + thickness + " px"
    }

    Repeater {
        model: [
            { thickness: 24, battery: true },
            { thickness: 32, battery: true },
            { thickness: 40, battery: true },
            { thickness: 56, battery: true },
            { thickness: 40, battery: false }
        ]

        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: "panel thickness " + modelData.thickness + " px, battery "
                      + (modelData.battery ? "shown" : "hidden")
                color: "#cccccc"
                font.pixelSize: 11
            }

            Rectangle {
                // a strip of exactly the thickness the panel would give
                Layout.fillWidth: true
                Layout.preferredHeight: modelData.thickness
                color: "#2a2e32"
                border.color: "#454b51"

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6

                    // a panel forces the height; do the same here
                    Ui.CompactRepresentation {
                        id: indicator
                        height: modelData.thickness
                        batteryLevel: 90
                        showBattery: modelData.battery
                    }

                    Text {
                        text: "← our indicator asks for " + indicator.implicitWidth.toFixed(0)
                              + " x " + indicator.implicitHeight.toFixed(0) + " px"
                        color: "#8ab4f8"
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
