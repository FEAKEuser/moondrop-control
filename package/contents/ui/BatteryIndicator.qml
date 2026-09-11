// SPDX-License-Identifier: GPL-3.0-or-later
// Small upright battery gauge: an SVG shell with a charge fill that rises from
// the bottom.
//
// The shell is a plain outline (see AppletArtwork.battery), 12x26 viewBox with
// the inner area at x 2.1..9.9, y 5.1..22.9.  Standing up instead of lying on
// its side keeps the panel footprint narrow.  The colour follows the level:
// normal, warning, low.
import QtQuick
import org.kde.kirigami as Kirigami

import "." as Artwork

Item {
    id: battery

    // charge in percent, -1 = unknown
    property int level: -1

    readonly property int clampedLevel: Math.max(0, Math.min(100, level))
    readonly property color fillColor: {
        if (clampedLevel <= 10) {
            return Kirigami.Theme.negativeTextColor
        }
        if (clampedLevel <= 25) {
            return Kirigami.Theme.neutralTextColor
        }
        return Kirigami.Theme.positiveTextColor
    }

    // geometry of the shell's inner area, in fractions of the viewBox (12 x 26)
    readonly property real innerX: width * (2.1 / 12)
    readonly property real innerWidth: width * ((9.9 - 2.1) / 12)
    readonly property real innerBottom: height * (22.9 / 26)
    readonly property real innerHeight: height * ((22.9 - 5.1) / 26)

    Image {
        anchors.fill: parent
        source: Artwork.AppletArtwork.url(Artwork.AppletArtwork.battery(Kirigami.Theme.textColor))
        sourceSize.width: battery.width
        sourceSize.height: battery.height
        smooth: true
        opacity: 0.9
    }

    Rectangle {
        id: charge

        // the charge grows upwards, like the liquid in a real cell
        property real fillHeight: battery.clampedLevel <= 0
                                  ? 0
                                  : Math.max(battery.innerHeight * battery.clampedLevel / 100, 1)

        x: battery.innerX
        width: battery.innerWidth
        y: battery.innerBottom - fillHeight
        height: fillHeight
        radius: Math.min(width / 2, Kirigami.Units.smallSpacing * 0.3)
        color: battery.fillColor

        Behavior on fillHeight {
            NumberAnimation { duration: Kirigami.Units.shortDuration }
        }
    }
}
