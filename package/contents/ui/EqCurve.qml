// SPDX-License-Identifier: MIT
// Interactive parametric EQ response curve.
//
// Draws the combined magnitude response of the bands on a logarithmic frequency
// axis and lets the user drag the band handles directly:
//
//   * drag a handle        -> frequency (horizontal) and gain (vertical)
//   * wheel over a handle  -> Q factor
//   * click a handle       -> select that band
//
// The response is computed with the RBJ audio EQ cookbook biquads, the same math
// the firmware uses, so what is drawn matches what the headphone will do.
import QtQuick
import org.kde.kirigami as Kirigami

Canvas {
    id: root

    // [{ frequency, gain, q, type }] — the edited state
    property var bands: []
    // the last state read from the device, drawn as a dashed reference
    property var referenceBands: []
    property int selectedIndex: -1
    // bump this whenever `bands` is mutated in place
    property int revision: 0

    property real minGain: -12
    property real maxGain: 12
    property real minFrequency: 20
    property real maxFrequency: 20000
    property int sampleRate: 48000
    property bool interactive: true

    // filter type of a band as understood by the firmware payload
    readonly property var filterTypes: [
        { value: 0, label: i18n("Peak") },
        { value: 1, label: i18n("Low shelf") },
        { value: 3, label: i18n("High shelf") }
    ]

    signal bandSelected(int index)
    signal bandMoved(int index, real frequency, real gain)
    signal bandQChanged(int index, real q)

    implicitHeight: Kirigami.Units.gridUnit * 10
    implicitWidth: Kirigami.Units.gridUnit * 20

    // Kirigami fonts usually carry a point size, so derive a pixel size for the
    // canvas text instead of reading pixelSize (which can be -1)
    readonly property int labelPixelSize: Math.max(8, Math.round(Kirigami.Units.gridUnit * 0.42))

    // ---- coordinate helpers ------------------------------------------------
    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value))
    }

    function frequencyToX(frequency) {
        return width * Math.log(frequency / root.minFrequency) / Math.log(root.maxFrequency / root.minFrequency)
    }

    function xToFrequency(x) {
        return root.minFrequency * Math.pow(root.maxFrequency / root.minFrequency, clamp(x / width, 0, 1))
    }

    function gainToY(gain) {
        return height * (1 - (gain - root.minGain) / (root.maxGain - root.minGain))
    }

    function yToGain(y) {
        return root.minGain + (1 - clamp(y / height, 0, 1)) * (root.maxGain - root.minGain)
    }

    // ---- biquad math (RBJ cookbook) ---------------------------------------
    function coefficients(band) {
        const gain = band.gain
        const A = Math.pow(10, gain / 40)
        const w0 = 2 * Math.PI * band.frequency / root.sampleRate
        const cosW0 = Math.cos(w0)
        const sinW0 = Math.sin(w0)
        const q = Math.max(0.05, band.q)

        if (band.type === 1 || band.type === 3) {
            // shelves: RBJ uses a slope S; the firmware reuses the Q field for it
            const alpha = (sinW0 / 2) * Math.sqrt((A + 1 / A) * (1 / q - 1) + 2)
            const beta = 2 * Math.sqrt(A) * alpha
            if (band.type === 1) {
                return {
                    b0: A * ((A + 1) - (A - 1) * cosW0 + beta),
                    b1: 2 * A * ((A - 1) - (A + 1) * cosW0),
                    b2: A * ((A + 1) - (A - 1) * cosW0 - beta),
                    a0: (A + 1) + (A - 1) * cosW0 + beta,
                    a1: -2 * ((A - 1) + (A + 1) * cosW0),
                    a2: (A + 1) + (A - 1) * cosW0 - beta
                }
            }
            return {
                b0: A * ((A + 1) + (A - 1) * cosW0 + beta),
                b1: -2 * A * ((A - 1) + (A + 1) * cosW0),
                b2: A * ((A + 1) + (A - 1) * cosW0 - beta),
                a0: (A + 1) - (A - 1) * cosW0 + beta,
                a1: 2 * ((A - 1) - (A + 1) * cosW0),
                a2: (A + 1) - (A - 1) * cosW0 - beta
            }
        }

        // peaking
        const alpha = sinW0 / (2 * q)
        return {
            b0: 1 + alpha * A,
            b1: -2 * cosW0,
            b2: 1 - alpha * A,
            a0: 1 + alpha / A,
            a1: -2 * cosW0,
            a2: 1 - alpha / A
        }
    }

    // magnitude of one biquad at a frequency, in dB
    function magnitudeDb(coefficients, frequency) {
        const w = 2 * Math.PI * frequency / root.sampleRate
        const cosW = Math.cos(w)
        const sinW = Math.sin(w)
        const cos2W = Math.cos(2 * w)
        const sin2W = Math.sin(2 * w)

        const numeratorReal = coefficients.b0 + coefficients.b1 * cosW + coefficients.b2 * cos2W
        const numeratorImag = -(coefficients.b1 * sinW + coefficients.b2 * sin2W)
        const denominatorReal = coefficients.a0 + coefficients.a1 * cosW + coefficients.a2 * cos2W
        const denominatorImag = -(coefficients.a1 * sinW + coefficients.a2 * sin2W)

        const numerator = Math.hypot(numeratorReal, numeratorImag)
        const denominator = Math.hypot(denominatorReal, denominatorImag)
        if (denominator === 0 || numerator === 0) {
            return 0
        }
        return 20 * Math.log10(numerator / denominator)
    }

    function bandDb(band, frequency) {
        // a band that is switched off contributes nothing
        if (band.gain === 0 && band.type === 0) {
            return 0
        }
        return magnitudeDb(coefficients(band), frequency)
    }

    function totalDb(frequency) {
        let sum = 0
        for (let i = 0; i < root.bands.length; ++i) {
            sum += bandDb(root.bands[i], frequency)
        }
        return sum
    }

    // ---- painting ----------------------------------------------------------
    readonly property var frequencyTicks: [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
    readonly property var gainTicks: [12, 6, 0, -6, -12]

    function formatFrequency(value) {
        if (value >= 1000) {
            const thousands = value / 1000
            return (thousands % 1 === 0 ? thousands.toFixed(0) : thousands.toFixed(1)) + "k"
        }
        return value.toFixed(0)
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()

        const w = width
        const h = height
        const curveTop = Kirigami.Units.smallSpacing
        const curveBottom = h - root.labelPixelSize - Kirigami.Units.smallSpacing * 2
        const plotHeight = curveBottom - curveTop

        // background
        ctx.fillStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                Kirigami.Theme.textColor.b, 0.06)
        ctx.fillRect(0, 0, w, h)

        const gainToPlotY = function (gain) {
            return curveTop + plotHeight * (1 - (gain - root.minGain) / (root.maxGain - root.minGain))
        }

        // horizontal grid + dB labels
        ctx.font = root.labelPixelSize + "px sans-serif"
        ctx.textBaseline = "middle"
        for (const gain of root.gainTicks) {
            const y = gainToPlotY(gain)
            ctx.strokeStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                      Kirigami.Theme.textColor.b, gain === 0 ? 0.35 : 0.12)
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(0, y)
            ctx.lineTo(w, y)
            ctx.stroke()

            if (gain !== root.maxGain && gain !== root.minGain) {
                ctx.fillStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                        Kirigami.Theme.textColor.b, 0.55)
                ctx.fillText((gain > 0 ? "+" : "") + gain, 3, y - root.labelPixelSize * 0.8)
            }
        }

        // vertical grid + frequency labels
        ctx.textBaseline = "bottom"
        for (const frequency of root.frequencyTicks) {
            const x = root.frequencyToX(frequency)
            if (x < 0 || x > w) {
                continue
            }
            ctx.strokeStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                      Kirigami.Theme.textColor.b, 0.12)
            ctx.beginPath()
            ctx.moveTo(x, curveTop)
            ctx.lineTo(x, curveBottom)
            ctx.stroke()

            if (frequency !== root.minFrequency && frequency !== root.maxFrequency) {
                ctx.fillStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                        Kirigami.Theme.textColor.b, 0.55)
                const label = root.formatFrequency(frequency)
                const labelWidth = ctx.measureText(label).width
                ctx.fillText(label, Math.min(w - labelWidth - 2, Math.max(2, x - labelWidth / 2)),
                             h - Kirigami.Units.smallSpacing)
            }
        }

        // dash out of range parts of the curve instead of clipping silently
        ctx.save()
        ctx.beginPath()
        ctx.rect(0, 0, w, h)
        ctx.clip()

        const samples = Math.max(120, Math.floor(w))
        const drawCurve = function (bandList, dashed, color, alpha, width) {
            if (!bandList || bandList.length === 0) {
                return
            }
            ctx.setLineDash(dashed ? [4, 4] : [])
            ctx.strokeStyle = Qt.rgba(color.r, color.g, color.b, alpha)
            ctx.lineWidth = width
            ctx.beginPath()
            for (let i = 0; i <= samples; ++i) {
                const x = w * i / samples
                const frequency = root.xToFrequency(x)
                let db = 0
                for (let b = 0; b < bandList.length; ++b) {
                    db += root.bandDb(bandList[b], frequency)
                }
                const y = gainToPlotY(root.clamp(db, root.minGain - 6, root.maxGain + 6))
                if (i === 0) {
                    ctx.moveTo(x, y)
                } else {
                    ctx.lineTo(x, y)
                }
            }
            ctx.stroke()
            ctx.setLineDash([])
        }

        // reference (what the headphone currently has) and the edited curve
        if (root.referenceBands && root.referenceBands.length > 0 && root.revision >= 0) {
            drawCurve(root.referenceBands, true, Kirigami.Theme.textColor, 0.45, 1)
        }
        drawCurve(root.bands, false, Kirigami.Theme.highlightColor, 1.0, 2)

        // handles
        for (let i = 0; i < root.bands.length; ++i) {
            const band = root.bands[i]
            const x = root.frequencyToX(band.frequency)
            const y = gainToPlotY(band.gain)
            const selected = (i === root.selectedIndex)
            const radius = selected ? Kirigami.Units.smallSpacing * 1.1
                                    : Kirigami.Units.smallSpacing * 0.85

            ctx.beginPath()
            ctx.arc(x, y, radius, 0, 2 * Math.PI)
            ctx.fillStyle = selected ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
            ctx.fill()
            ctx.lineWidth = selected ? 2 : 1.5
            ctx.strokeStyle = Kirigami.Theme.highlightColor
            ctx.stroke()

            // the band number next to the handle, on an opaque chip so it stays
            // readable where it crosses the curve
            if (selected) {
                const label = (i + 1) + ": " + Math.round(band.frequency) + " Hz  "
                              + (band.gain >= 0 ? "+" : "") + band.gain.toFixed(1) + " dB  Q "
                              + band.q.toFixed(2)
                ctx.font = root.labelPixelSize + "px sans-serif"
                const labelWidth = ctx.measureText(label).width
                const padding = 3
                const labelX = root.clamp(x + radius + 3, 2, w - labelWidth - padding * 2 - 2)
                let labelY = y - radius - 2
                if (labelY < root.labelPixelSize * 1.6) {
                    labelY = y + radius + root.labelPixelSize * 1.6
                }
                ctx.fillStyle = Qt.rgba(Kirigami.Theme.backgroundColor.r,
                                        Kirigami.Theme.backgroundColor.g,
                                        Kirigami.Theme.backgroundColor.b, 0.85)
                ctx.fillRect(labelX - padding, labelY - root.labelPixelSize - padding * 0.5,
                             labelWidth + padding * 2, root.labelPixelSize + padding * 1.5)
                ctx.fillStyle = Kirigami.Theme.textColor
                ctx.textBaseline = "bottom"
                ctx.fillText(label, labelX, labelY)
            }
        }

        ctx.restore()
    }

    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onBandsChanged: requestPaint()
    onReferenceBandsChanged: requestPaint()
    onSelectedIndexChanged: requestPaint()
    onRevisionChanged: requestPaint()
    onSampleRateChanged: requestPaint()

    // ---- interaction -------------------------------------------------------
    function handleAt(x, y) {
        let best = -1
        let bestDistance = Kirigami.Units.smallSpacing * 2.2
        for (let i = 0; i < root.bands.length; ++i) {
            const band = root.bands[i]
            const dx = root.frequencyToX(band.frequency) - x
            const dy = root.gainToY(band.gain) - y
            const distance = Math.hypot(dx, dy)
            if (distance < bestDistance) {
                bestDistance = distance
                best = i
            }
        }
        return best
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.interactive
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: pressedIndex >= 0 ? Qt.SizeAllCursor : Qt.ArrowCursor

        property int pressedIndex: -1

        onPressed: mouseEvent => {
            const index = root.handleAt(mouseEvent.x, mouseEvent.y)
            if (index >= 0) {
                pressedIndex = index
                root.bandSelected(index)
            }
        }

        onPositionChanged: mouseEvent => {
            if (pressedIndex < 0 || !pressed) {
                return
            }
            const band = root.bands[pressedIndex]
            if (!band) {
                return
            }
            const frequency = root.clamp(root.xToFrequency(mouseEvent.x), 20, 20000)
            const gain = root.clamp(root.yToGain(mouseEvent.y), root.minGain, root.maxGain)
            // keep the value the user sees while dragging consistent with the
            // rounded value that will be sent to the device
            const rounded = Math.round(frequency)
            root.bandMoved(pressedIndex, rounded, Math.round(gain * 10) / 10)
        }

        onReleased: pressedIndex = -1
        onCanceled: pressedIndex = -1

        onWheel: wheelEvent => {
            const index = root.handleAt(wheelEvent.x, wheelEvent.y)
            const target = index >= 0 ? index : root.selectedIndex
            const band = root.bands[target]
            if (!band) {
                return
            }
            const step = wheelEvent.angleDelta.y > 0 ? 1.08 : 1 / 1.08
            root.bandQChanged(target, root.clamp(band.q * step, 0.1, 10))
            wheelEvent.accepted = true
        }
    }
}
