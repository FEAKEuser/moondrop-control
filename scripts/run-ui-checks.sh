#!/usr/bin/env bash
# Renders the QML checks in tests/ (and optionally a page or model variant).
#
# The checks import the applet's UI directory as a module.  That only resolves
# correctly when the importing file sits *next to* it, because a directory import
# is relative to the importing file; outside the package the singleton in
# contents/ui/qmldir is not found.  So the check files are copied into the
# package directory for the duration of the render.
#
#   ./scripts/run-ui-checks.sh [output directory]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-/tmp/moondrop-ui-checks}"
UI_DIR="$ROOT/package/contents/ui"
PREVIEW="$ROOT/build/cli/preview"

[ -x "$PREVIEW" ] || { echo "error: build the project first (cmake --build build)" >&2; exit 1; }

mkdir -p "$OUT"
cp "$ROOT"/tests/*.qml "$UI_DIR/" 2>/dev/null || true
trap 'rm -f "$UI_DIR"/output-gain.qml "$UI_DIR"/eq-curve.qml "$UI_DIR"/compact.qml "$UI_DIR"/panel-sizing.qml' EXIT

declare -A sizes=(
    [output-gain]="460 560"
    [eq-curve]="520 700"
    [compact]="430 320"
    [panel-sizing]="460 330"
)

for name in output-gain eq-curve compact panel-sizing; do
    src="$ROOT/tests/$name.qml"
    [ -f "$src" ] || continue
    props="package/contents/ui/$name.qml"
    out="$OUT/$name.png"
    # shellcheck disable=SC2086
    ( cd "$ROOT" && MOONDROP_PREVIEW_NOCONNECT=1 QT_QPA_PLATFORM=offscreen \
        "$PREVIEW" "$props" "$out" ${sizes[$name]} )
done

echo "rendered into $OUT"
