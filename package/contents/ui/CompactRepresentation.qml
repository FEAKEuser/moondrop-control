// SPDX-License-Identifier: GPL-3.0-or-later
// Panel indicator.
//
// Sizing rules for a Plasma panel applet:
//   * never ask for more space than the panel gives - the item is centred inside
//     whatever the panel allocates, and the Layout maximums below keep it from
//     growing into the neighbours;
//   * the glyph size follows the panel thickness, so it lines up with the
//     surrounding tray icons instead of towering over them.
//
// The charge is shown as a small *upright* battery whose fill rises from the
// bottom: it needs less than half the width of the "90 %" text it replaces, and
// width is the scarce axis in a horizontal panel.
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

import "." as Artwork

Item {
    id: compact

    // -1 = unknown
    property int batteryLevel: -1
    property bool showBattery: true

    readonly property bool connected: Moondrop.connected
    // In a panel this item's height *is* the panel thickness.  The fallback
    // covers being placed somewhere that gives no height (desktop, tooltip).
    readonly property real panelThickness: compact.height > 0
                                           ? compact.height
                                           : Kirigami.Units.iconSizes.smallMedium
    // stay a little below the thickness so the glyph does not touch the edges
    readonly property real glyphSize: Math.round(Math.max(Kirigami.Units.iconSizes.small,
                                                          Math.min(Kirigami.Units.iconSizes.smallMedium,
                                                                   panelThickness - Kirigami.Units.smallSpacing * 2)))

    // 12:26 like the battery artwork
    readonly property real batteryHeight: Math.round(glyphSize * 0.86)
    readonly property real batteryWidth: Math.round(batteryHeight * 12 / 26)

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    // hard limits: the applet must not push its neighbours aside
    Layout.maximumWidth: implicitWidth
    Layout.maximumHeight: implicitHeight
    Layout.preferredWidth: implicitWidth
    Layout.preferredHeight: implicitHeight

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Math.round(Kirigami.Units.smallSpacing * 0.5)

        Image {
            source: Artwork.AppletArtwork.url(Artwork.AppletArtwork.headphones(Kirigami.Theme.textColor))
            sourceSize.width: compact.glyphSize
            sourceSize.height: compact.glyphSize
            Layout.preferredWidth: compact.glyphSize
            Layout.preferredHeight: compact.glyphSize
            smooth: true
            // dimmed while the headphone is not reachable
            opacity: compact.connected ? 1.0 : 0.55
        }

        BatteryIndicator {
            visible: compact.showBattery && compact.batteryLevel >= 0
            level: compact.batteryLevel
            Layout.preferredWidth: compact.batteryWidth
            Layout.preferredHeight: compact.batteryHeight
            Layout.alignment: Qt.AlignVCenter
            opacity: compact.connected ? 1.0 : 0.55
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.expanded = !root.expanded
    }
}
