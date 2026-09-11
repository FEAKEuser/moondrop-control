// Renders the parametric EQ curve with a few known settings, so the drawn
// response can be compared against the numbers:
//
//   ./build/cli/preview tests/eq-curve.qml /tmp/curve.png 520 640
//
// Expected: a +6 dB peak at 1 kHz must reach the +6 line, a flat setting must be
// a straight line on 0 dB, and the band handles must sit on the curve.
import QtQuick
import QtQuick.Layouts
import "." as Ui

ColumnLayout {
    spacing: 10

    Repeater {
        model: [
            {
                title: "flat (all 0 dB)",
                bands: [
                    { frequency: 23, gain: 0, q: 0.4, type: 0 },
                    { frequency: 240, gain: 0, q: 5.1, type: 0 },
                    { frequency: 1400, gain: 0, q: 6.3, type: 0 },
                    { frequency: 2300, gain: 0, q: 1.8, type: 0 },
                    { frequency: 6900, gain: 0, q: 1.2, type: 0 }
                ]
            },
            {
                title: "single +6 dB peak at 1 kHz, Q 2",
                bands: [
                    { frequency: 1000, gain: 6, q: 2, type: 0 },
                    { frequency: 240, gain: 0, q: 1, type: 0 },
                    { frequency: 4000, gain: 0, q: 1, type: 0 },
                    { frequency: 8000, gain: 0, q: 1, type: 0 },
                    { frequency: 16000, gain: 0, q: 1, type: 0 }
                ]
            },
            {
                title: "single -6 dB dip at 1 kHz, Q 2",
                bands: [
                    { frequency: 1000, gain: -6, q: 2, type: 0 },
                    { frequency: 240, gain: 0, q: 1, type: 0 },
                    { frequency: 4000, gain: 0, q: 1, type: 0 },
                    { frequency: 8000, gain: 0, q: 1, type: 0 },
                    { frequency: 16000, gain: 0, q: 1, type: 0 }
                ]
            },
            {
                title: "bass shelf +6 dB below 200 Hz (low shelf)",
                bands: [
                    { frequency: 200, gain: 6, q: 0.7, type: 1 },
                    { frequency: 1000, gain: 0, q: 1, type: 0 },
                    { frequency: 4000, gain: 0, q: 1, type: 0 },
                    { frequency: 8000, gain: 0, q: 1, type: 0 },
                    { frequency: 16000, gain: 0, q: 1, type: 0 }
                ]
            },
            {
                title: "the measured EDGE curve, with a dashed reference",
                bands: [
                    { frequency: 23, gain: 3.0, q: 0.4, type: 0 },
                    { frequency: 240, gain: 2.7, q: 5.1, type: 0 },
                    { frequency: 1400, gain: 3.0, q: 6.3, type: 0 },
                    { frequency: 2300, gain: -2.9, q: 1.8, type: 0 },
                    { frequency: 6900, gain: 3.0, q: 1.2, type: 0 }
                ],
                reference: [
                    { frequency: 23, gain: -9, q: 0.4, type: 0 },
                    { frequency: 240, gain: -4, q: 5.1, type: 0 },
                    { frequency: 1400, gain: 0, q: 6.3, type: 0 },
                    { frequency: 2300, gain: -2.9, q: 1.8, type: 0 },
                    { frequency: 6900, gain: 3.0, q: 1.2, type: 0 }
                ]
            }
        ]

        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: modelData.title + "   peak: " + checkPeak(modelData.bands).toFixed(2) + " dB"
                color: "#cccccc"
            }

            Ui.EqCurve {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                bands: modelData.bands
                referenceBands: modelData.reference === undefined ? [] : modelData.reference
                selectedIndex: 0
                interactive: false
            }
        }
    }

    // numeric cross check: the curve has to be a plain sum of the biquads
    function checkPeak(bands) {
        let sum = 0
        for (let i = 0; i < bands.length; ++i) {
            const b = bands[i]
            const A = Math.pow(10, b.gain / 40)
            const w0 = 2 * Math.PI * b.frequency / 48000
            const alpha = Math.sin(w0) / (2 * b.q)
            const b0 = 1 + alpha * A, b1 = -2 * Math.cos(w0), b2 = 1 - alpha * A
            const a0 = 1 + alpha / A, a1 = -2 * Math.cos(w0), a2 = 1 - alpha / A
            const w = 2 * Math.PI * b.frequency / 48000
            const cw = Math.cos(w), sw = Math.sin(w), c2w = Math.cos(2 * w), s2w = Math.sin(2 * w)
            const nr = b0 + b1 * cw + b2 * c2w, ni = -(b1 * sw + b2 * s2w)
            const dr = a0 + a1 * cw + a2 * c2w, di = -(a1 * sw + a2 * s2w)
            sum += 20 * Math.log10(Math.hypot(nr, ni) / Math.hypot(dr, di))
        }
        return sum
    }
}
