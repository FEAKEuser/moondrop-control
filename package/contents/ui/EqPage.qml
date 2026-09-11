// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

import org.moondrop.backend 1.0

PlasmaComponents3.ScrollView {
    id: page

    // The edited state lives here as a plain array; `revision` is bumped on every
    // change so the curve (and anything else bound to it) repaints.
    property var bandList: []
    property int revision: 0
    property int selectedBand: 0
    property bool dirty: false
    property bool showImport: false
    property bool showAdvanced: false
    property string importError: ""

    readonly property int bandCount: bandList.length
    readonly property int sampleRateHz: Plasmoid.configuration.sampleRate > 0
                                         ? Plasmoid.configuration.sampleRate : 48000
    readonly property bool writing: Moondrop.pendingBands.length > 0
    readonly property bool haveDeviceCurve: Moondrop.bands.length > 0

    readonly property var currentBand: (selectedBand >= 0 && selectedBand < bandList.length)
                                        ? bandList[selectedBand] : null

    readonly property var currentPresetIndex: {
        for (let i = 0; i < Moondrop.presetIds.length; ++i) {
            if (Moondrop.presetIds[i] === Moondrop.currentPreset) {
                return i
            }
        }
        return -1
    }

    function loadFromDevice() {
        const source = Moondrop.bands
        const list = []
        for (let i = 0; i < source.length; ++i) {
            list.push({
                frequency: source[i].frequency,
                gain: source[i].gain,
                q: source[i].q,
                type: source[i].type === undefined ? 0 : source[i].type
            })
        }
        page.bandList = list
        if (page.selectedBand >= list.length) {
            page.selectedBand = 0
        }
        page.dirty = false
        page.revision++
    }

    function updateBand(index, properties) {
        if (index < 0 || index >= page.bandList.length) {
            return
        }
        const list = page.bandList.slice()
        list[index] = Object.assign({}, list[index], properties)
        page.bandList = list
        page.dirty = true
        page.revision++
    }

    function flatten() {
        const list = []
        for (let i = 0; i < page.bandList.length; ++i) {
            list.push(Object.assign({}, page.bandList[i], { gain: 0, q: 1 }))
        }
        page.bandList = list
        page.dirty = true
        page.revision++
    }

    // Accepts the shapes people actually copy around:
    //   "Filter 1: ON PK Fc 23 Hz Gain -1.5 dB Q 0.40"   (AutoEq / EqualizerAPO)
    //   "23:-1.5:0.4; 240:2.7:5.1; ..."                  (cli style)
    //   one "frequency gain q" triple per line
    function parseImport(text) {
        const bands = []
        const lines = text.split(/[\n;]+/)
        for (const rawLine of lines) {
            const line = rawLine.trim()
            if (line.length === 0) {
                continue
            }
            const filter = line.match(/Fc\s+([\d.]+)\s*Hz/i)
            if (filter) {
                const gain = line.match(/Gain\s+(-?[\d.]+)\s*dB/i)
                const q = line.match(/Q\s+([\d.]+)/i)
                const type = line.match(/\b(ON|OFF)\s+(\w+)/i)
                let filterType = 0
                if (type && /HSQ|HSC|HS/i.test(type[2])) {
                    filterType = 3
                } else if (type && /LSQ|LSC|LS/i.test(type[2])) {
                    filterType = 1
                }
                bands.push({
                    frequency: Math.round(parseFloat(filter[1])),
                    gain: gain ? parseFloat(gain[1]) : 0,
                    q: q ? parseFloat(q[1]) : 1,
                    type: filterType
                })
                continue
            }
            const fields = line.split(/[\s:,]+/).filter(part => part.length > 0)
            if (fields.length >= 3) {
                bands.push({
                    frequency: Math.round(parseFloat(fields[0])),
                    gain: parseFloat(fields[1]),
                    q: parseFloat(fields[2]),
                    type: 0
                })
            }
        }
        if (bands.length === 0) {
            return null
        }
        for (const band of bands) {
            if (isNaN(band.frequency) || isNaN(band.gain) || isNaN(band.q)) {
                return null
            }
        }
        return bands
    }

    function applyImport() {
        const parsed = page.parseImport(importField.text)
        if (!parsed) {
            page.importError = i18n("Could not read a filter list from that text.")
            return
        }
        if (parsed.length !== page.bandList.length) {
            page.importError = i18n("The headphone has %1 bands, the text has %2.",
                                    page.bandList.length, parsed.length)
            return
        }
        // keep the device's own frequencies when the text has fewer bands than
        // the headphone, otherwise take everything from the text
        const list = []
        for (let i = 0; i < parsed.length; ++i) {
            list.push({
                frequency: Math.max(20, Math.min(20000, parsed[i].frequency)),
                gain: Math.max(-12, Math.min(3, parsed[i].gain)),
                q: Math.max(0.1, Math.min(10, parsed[i].q)),
                type: parsed[i].type
            })
        }
        page.bandList = list
        page.dirty = true
        page.revision++
        page.showImport = false
        page.importError = ""
    }

    Component.onCompleted: loadFromDevice()

    Connections {
        target: Moondrop
        function onEqChanged() {
            // never discard edits that are still being made
            if (!page.dirty) {
                page.loadFromDevice()
            }
        }
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Label {
                text: i18n("Preset")
            }

            PlasmaComponents3.ComboBox {
                id: presetBox
                Layout.fillWidth: true
                enabled: Moondrop.connected && !Moondrop.busy && Moondrop.presetIds.length > 0
                model: Moondrop.presetIds
                currentIndex: page.currentPresetIndex
                displayText: currentIndex >= 0 ? Moondrop.presetName(Moondrop.presetIds[currentIndex])
                                               : i18n("unknown")
                delegate: PlasmaComponents3.ItemDelegate {
                    required property int index
                    width: presetBox.width
                    text: Moondrop.presetName(Moondrop.presetIds[index])
                    highlighted: presetBox.highlightedIndex === index
                    onClicked: {
                        presetBox.currentIndex = index
                        Moondrop.selectPreset(Moondrop.presetIds[index])
                        presetBox.popup.close()
                    }
                }
                onActivated: index => Moondrop.selectPreset(Moondrop.presetIds[index])
            }
        }

        EqCurve {
            id: curve
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 9
            Layout.minimumHeight: Kirigami.Units.gridUnit * 6
            visible: page.bandCount > 0
            bands: page.bandList
            referenceBands: page.dirty ? Moondrop.bands : []
            revision: page.revision
            selectedIndex: page.selectedBand
            sampleRate: page.sampleRateHz
            interactive: Moondrop.connected || page.dirty
            onBandSelected: index => page.selectedBand = index
            onBandMoved: (index, frequency, gain) => page.updateBand(index, { frequency: frequency, gain: gain })
            onBandQChanged: (index, q) => page.updateBand(index, { q: q })
        }

        ConnectionHint {}

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: page.bandCount === 0 && Moondrop.connected
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: i18n("This headphone does not report a parametric equalizer.")
        }

        // band chips
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: page.bandCount > 0

            Repeater {
                model: page.bandList

                PlasmaComponents3.Button {
                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    checkable: true
                    checked: page.selectedBand === index
                    display: PlasmaComponents3.AbstractButton.TextOnly
                    font: Kirigami.Theme.smallFont
                    text: (index + 1) + "\n" + (modelData.gain >= 0 ? "+" : "") + modelData.gain.toFixed(1)
                    onClicked: page.selectedBand = index
                    PlasmaComponents3.ToolTip {
                        text: i18n("Band %1: %2 Hz, %3 dB, Q %4",
                                   index + 1, Math.round(modelData.frequency),
                                   modelData.gain.toFixed(1), modelData.q.toFixed(2))
                    }
                }
            }
        }

        // details of the selected band
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Kirigami.Units.smallSpacing
            visible: page.currentBand !== null
            enabled: page.currentBand !== null

            PlasmaComponents3.Label {
                text: i18n("Frequency")
                Layout.alignment: Qt.AlignRight
            }
            PlasmaComponents3.SpinBox {
                Layout.fillWidth: true
                from: 20
                to: 20000
                stepSize: 10
                editable: true
                value: page.currentBand ? page.currentBand.frequency : 1000
                onValueModified: page.updateBand(page.selectedBand, { frequency: value })
            }
            PlasmaComponents3.Label {
                text: i18n("Hz")
                opacity: 0.7
            }

            PlasmaComponents3.Label {
                text: i18n("Gain")
                Layout.alignment: Qt.AlignRight
            }
            PlasmaComponents3.Slider {
                id: gainSlider
                Layout.fillWidth: true
                from: -12
                to: 3
                stepSize: 0.1
                value: page.currentBand ? page.currentBand.gain : 0
                onMoved: page.updateBand(page.selectedBand, { gain: value })
            }
            PlasmaComponents3.Label {
                Layout.minimumWidth: Kirigami.Units.gridUnit * 2.6
                horizontalAlignment: Text.AlignRight
                text: gainSlider.value.toFixed(1) + " dB"
            }

            PlasmaComponents3.Label {
                text: i18n("Q factor")
                Layout.alignment: Qt.AlignRight
            }
            PlasmaComponents3.Slider {
                id: qSlider
                Layout.fillWidth: true
                from: 0.1
                to: 10
                stepSize: 0.05
                value: page.currentBand ? page.currentBand.q : 1
                onMoved: page.updateBand(page.selectedBand, { q: value })
            }
            PlasmaComponents3.Label {
                Layout.minimumWidth: Kirigami.Units.gridUnit * 2.6
                horizontalAlignment: Text.AlignRight
                text: qSlider.value.toFixed(2)
            }
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: page.bandCount > 0
            opacity: 0.6
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            text: i18n("Drag a point to set frequency and gain, scroll over it to change Q.")
        }

        // actions
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Button {
                Layout.fillWidth: true
                enabled: Moondrop.connected && page.bandCount > 0 && !Moondrop.busy
                icon.name: "document-save"
                text: page.dirty ? i18n("Apply*") : i18n("Apply")
                onClicked: {
                    Moondrop.applyUserEq(page.bandList, true)
                    page.dirty = false
                }
                PlasmaComponents3.ToolTip {
                    text: i18n("Write the bands to the headphone and switch to the custom preset")
                }
            }
            PlasmaComponents3.Button {
                enabled: page.dirty
                icon.name: "edit-undo"
                onClicked: page.loadFromDevice()
                PlasmaComponents3.ToolTip {
                    text: i18n("Discard the changes and load what the headphone reports")
                }
            }
            PlasmaComponents3.Button {
                enabled: page.bandCount > 0
                icon.name: "edit-reset"
                onClicked: page.flatten()
                PlasmaComponents3.ToolTip {
                    text: i18n("Set every gain to 0 dB")
                }
            }
            PlasmaComponents3.Button {
                enabled: Moondrop.connected && Moondrop.hasDeviceEqBackup && !Moondrop.busy
                icon.name: "document-revert"
                onClicked: Moondrop.restoreDeviceEq()
                PlasmaComponents3.ToolTip {
                    text: i18n("Put back the curve the headphone reported earlier")
                }
            }
            PlasmaComponents3.Button {
                checkable: true
                checked: page.showImport
                icon.name: "edit-paste"
                onToggled: {
                    page.showImport = checked
                    page.showAdvanced = false
                }
                PlasmaComponents3.ToolTip {
                    text: i18n("Paste a filter list (AutoEq, EqualizerAPO, ...)")
                }
            }
            PlasmaComponents3.Button {
                checkable: true
                checked: page.showAdvanced
                icon.name: "configure"
                onToggled: {
                    page.showAdvanced = checked
                    page.showImport = false
                }
                PlasmaComponents3.ToolTip {
                    text: i18n("Filter type and curve calculation")
                }
            }
        }

        // import
        ColumnLayout {
            Layout.fillWidth: true
            visible: page.showImport
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.TextArea {
                id: importField
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 4
                placeholderText: i18n("Paste lines like:\nFilter 1: ON PK Fc 23 Hz Gain -1.5 dB Q 0.40")
                onTextChanged: page.importError = ""
            }
            RowLayout {
                Layout.fillWidth: true
                PlasmaComponents3.Button {
                    Layout.fillWidth: true
                    enabled: importField.text.length > 0
                    text: i18n("Load into the %1 bands", page.bandCount)
                    onClicked: page.applyImport()
                }
                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    visible: page.importError.length > 0
                    text: page.importError
                    color: Kirigami.Theme.negativeTextColor
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                }
            }
        }

        // advanced
        GridLayout {
            Layout.fillWidth: true
            visible: page.showAdvanced
            columns: 3
            columnSpacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Label {
                text: i18n("Filter type")
                Layout.alignment: Qt.AlignRight
            }
            PlasmaComponents3.ComboBox {
                id: typeBox
                Layout.fillWidth: true
                enabled: page.currentBand !== null
                textRole: "label"
                valueRole: "value"
                model: curve.filterTypes
                currentIndex: {
                    if (!page.currentBand) {
                        return 0
                    }
                    for (let i = 0; i < curve.filterTypes.length; ++i) {
                        if (curve.filterTypes[i].value === page.currentBand.type) {
                            return i
                        }
                    }
                    return 0
                }
                onActivated: index => page.updateBand(page.selectedBand,
                                                      { type: curve.filterTypes[index].value })
                PlasmaComponents3.ToolTip {
                    text: i18n("Shelving filters are part of the protocol, but MOONDROP EDGE always reports “Peak”; the headphone may ignore this.")
                }
            }
            PlasmaComponents3.Label {
                text: ""
            }

            PlasmaComponents3.Label {
                text: i18n("Curve at")
                Layout.alignment: Qt.AlignRight
            }
            PlasmaComponents3.ComboBox {
                Layout.fillWidth: true
                model: [44100, 48000, 96000]
                currentIndex: model.indexOf(page.sampleRateHz) >= 0 ? model.indexOf(page.sampleRateHz) : 1
                onActivated: index => Plasmoid.configuration.sampleRate = model[index]
                displayText: (currentValue / 1000).toFixed(1) + " kHz"
            }
            PlasmaComponents3.Label {
                text: ""
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: page.writing
            spacing: Kirigami.Units.smallSpacing
            PlasmaComponents3.BusyIndicator {
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                running: true
            }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: i18n("Writing the EQ to the headphone…")
                opacity: 0.7
                font: Kirigami.Theme.smallFont
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
