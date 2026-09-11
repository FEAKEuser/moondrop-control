// SPDX-License-Identifier: GPL-3.0-or-later
// Vector artwork of the applet, embedded as SVG.
//
// The SVGs live in QML string templates instead of separate files because
// Image.source resolves relative URLs against the *process* working directory
// rather than the QML file, which breaks as soon as the package is installed
// somewhere other than the current directory.  `source: "data:image/svg+xml;..."
// always works.  Rendering happens at the requested size, so the artwork stays
// crisp at any panel thickness.
//
// The templates take the colour as %1, so the drawing follows the Plasma theme.
pragma Singleton

import QtQuick

QtObject {
    // Headphones: rounded headband with two ear cups.  24x24 viewBox.
    readonly property string headphonesTemplate:
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" +
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"24\" height=\"24\">" +
        "<path d=\"M4.6 14.2 V 12 A 7.4 7.4 0 0 1 19.4 12 V 14.2\" fill=\"none\" stroke=\"%1\" " +
        "stroke-width=\"2.1\" stroke-linecap=\"round\"/>" +
        "<rect x=\"2.1\" y=\"12.6\" width=\"5.3\" height=\"8.6\" rx=\"2.65\" fill=\"%1\"/>" +
        "<rect x=\"16.6\" y=\"12.6\" width=\"5.3\" height=\"8.6\" rx=\"2.65\" fill=\"%1\"/>" +
        "</svg>"

    // Battery shell, upright: outline plus the terminal nub on top.
    // 12x26 viewBox; the area the charge fill maps to is x 2.1..9.9,
    // y 5.1..22.9 (see BatteryIndicator.qml).  Upright keeps the panel footprint
    // narrow, which matters more than height in a horizontal panel.
    readonly property string batteryTemplate:
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" +
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 12 26\" width=\"12\" height=\"26\">" +
        "<rect x=\"4.1\" y=\"1.0\" width=\"3.8\" height=\"2.3\" rx=\"1.15\" fill=\"%1\"/>" +
        "<rect x=\"1.05\" y=\"4.05\" width=\"9.9\" height=\"19.9\" rx=\"2.6\" fill=\"none\" " +
        "stroke=\"%1\" stroke-width=\"2.1\"/>" +
        "</svg>"

    // Filled in with the wanted colour
    function headphones(color) {
        return headphonesTemplate.arg(color)
    }

    function battery(color) {
        return batteryTemplate.arg(color)
    }

    // Encodes an SVG string for use with Image.source
    function url(svg) {
        return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg)
    }
}
