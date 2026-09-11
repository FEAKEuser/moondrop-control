// Renders the panel indicator at the sizes a Plasma panel actually uses, plus
// the battery at several charge levels, so the SVG artwork and the sizing can be
// judged without a panel:
//
//   ./build/cli/preview tests/compact.qml /tmp/compact.png 420 420
import QtQuick
import QtQuick.Layouts
import "." as Ui
import org.kde.kirigami as Kirigami

ColumnLayout {
    spacing: 14

    Text { text: "battery, upright (14 x 30 px)"; color: "#cccccc" }

    RowLayout {
        spacing: 10
        Repeater {
            model: [0, 5, 15, 50, 90, 100, -1]
            ColumnLayout {
                required property int modelData
                spacing: 2
                Ui.BatteryIndicator {
                    width: 14
                    height: 30
                    level: modelData
                }
                Text {
                    text: modelData + "%"
                    color: "#999999"
                    font.pixelSize: 9
                }
            }
        }
    }

    Text { text: "headphone glyph at panel sizes"; color: "#cccccc" }
    RowLayout {
        spacing: 8
        Repeater {
            model: [16, 22, 32, 48, 96]
            Image {
                required property int modelData
                source: Ui.AppletArtwork.url(Ui.AppletArtwork.headphones(Kirigami.Theme.textColor))
                sourceSize.width: modelData
                sourceSize.height: modelData
                width: modelData
                height: modelData
                smooth: true
            }
        }
    }

    Text { text: "full indicator at 24 / 32 / 40 / 56 px panel thickness"; color: "#cccccc" }
    ColumnLayout {
        spacing: 3
        Repeater {
            model: [24, 32, 40, 56]
            Rectangle {
                required property int modelData
                Layout.preferredWidth: 200
                Layout.preferredHeight: modelData
                color: "#2a2e32"
                border.color: "#454b51"
                Row {
                    anchors.centerIn: parent
                    spacing: 12
                    Ui.CompactRepresentation {
                        height: parent.parent.height
                        batteryLevel: 90
                        showBattery: true
                    }
                    Ui.CompactRepresentation {
                        height: parent.parent.height
                        batteryLevel: 42
                        showBattery: true
                    }
                }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
