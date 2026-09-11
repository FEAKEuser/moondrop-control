// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

PlasmaComponents3.ScrollView {
    id: page

    readonly property bool gainReversed: Plasmoid.configuration.gainReversed

    ColumnLayout {
        width: page.availableWidth
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            text: i18n("Bluetooth codec")
            font.bold: true
        }

        PlasmaComponents3.Switch {
            Layout.fillWidth: true
            visible: Moondrop.ldacSupported
            enabled: Moondrop.connected && !Moondrop.busy
            text: i18n("LDAC (high resolution audio)")
            checked: Moondrop.ldacEnabled
            onToggled: Moondrop.setLdacEnabled(checked)
        }

        PlasmaComponents3.Switch {
            Layout.fillWidth: true
            visible: Moondrop.lc3Supported
            enabled: Moondrop.connected && !Moondrop.busy
            text: i18n("LC3")
            checked: Moondrop.lc3Enabled
            onToggled: Moondrop.setLc3Enabled(checked)
        }

        PlasmaComponents3.Switch {
            Layout.fillWidth: true
            visible: Moondrop.lhdcSupported
            enabled: Moondrop.connected && !Moondrop.busy
            text: i18n("LHDC")
            checked: Moondrop.lhdcEnabled
            onToggled: Moondrop.setLhdcEnabled(checked)
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: !Moondrop.ldacSupported && !Moondrop.lc3Supported && !Moondrop.lhdcSupported
            wrapMode: Text.WordWrap
            opacity: 0.7
            font: Kirigami.Theme.smallFont
            text: i18n("This headphone does not expose a codec switch.")
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: Moondrop.dacGainSupported
            text: i18n("Output gain")
            font.bold: true
        }

        OutputGain {
            Layout.fillWidth: true
            visible: Moondrop.dacGainSupported
            deviceGain: Moondrop.dacGain
            reversed: page.gainReversed
            interactive: Moondrop.connected && !Moondrop.busy
            onGainSelected: gain => Moondrop.setDacGain(gain)
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: Moondrop.multipointSupported
            text: i18n("Connections")
            font.bold: true
        }

        PlasmaComponents3.Switch {
            Layout.fillWidth: true
            visible: Moondrop.multipointSupported
            enabled: Moondrop.connected && !Moondrop.busy
            text: i18n("Multipoint (two devices at once)")
            checked: Moondrop.multipointEnabled
            onToggled: Moondrop.setMultipointEnabled(checked)
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: Moondrop.dacGainSupported
            wrapMode: Text.WordWrap
            opacity: 0.7
            font: Kirigami.Theme.smallFont
            text: i18n("The headphone numbers the gain levels in reverse order (0 = highest). If your model does not, turn this around in the widget settings.")
        }

        ConnectionHint {}

        Item {
            Layout.fillHeight: true
        }
    }
}
