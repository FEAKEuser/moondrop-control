#!/usr/bin/env bash
# Install (or upgrade) the Moondrop Control Plasma applet for the current user.
#
#   ./install-applet.sh          install / upgrade
#   ./install-applet.sh remove   uninstall
#
# The applet package is looked up in this order, so the script works both from
# the source tree and after `cmake --install`:
#   1. $MOONDROP_PACKAGE_DIR (explicit override)
#   2. <prefix>/share/moondrop-widget/package, where <prefix> is derived from
#      where this script itself lives (<prefix>/bin/moondrop-widget-install-applet).
#      Deriving it matters: the install prefix is /usr/local on many systems,
#      and a hardcoded /usr/share then points at nothing.
#   3. ../package relative to the script (source tree)
#   4. the well known system locations
set -euo pipefail

# Resolve this script (following symlinks) so <prefix> can be derived from it.
script_path="${BASH_SOURCE[0]}"
if command -v readlink >/dev/null 2>&1; then
    resolved="$(readlink -f "$script_path" 2>/dev/null || true)"
    [ -n "$resolved" ] && script_path="$resolved"
fi
script_dir="$(cd "$(dirname "$script_path")" && pwd)"
prefix="$(dirname "$script_dir")"

find_package_dir() {
    local candidates=(
        "${MOONDROP_PACKAGE_DIR:-}"
        "$prefix/share/moondrop-widget/package"
        "$script_dir/../package"
        "/usr/share/moondrop-widget/package"
        "/usr/local/share/moondrop-widget/package"
    )
    local candidate
    for candidate in "${candidates[@]}"; do
        [ -n "$candidate" ] || continue
        if [ -f "$candidate/metadata.json" ]; then
            (cd "$candidate" && pwd)
            return 0
        fi
    done
    return 1
}

PKG="$(find_package_dir || true)"
if [ -z "$PKG" ]; then
    echo "error: applet package not found (no metadata.json)." >&2
    echo "Looked in:" >&2
    echo "    \$MOONDROP_PACKAGE_DIR (${MOONDROP_PACKAGE_DIR:-unset})" >&2
    echo "    $prefix/share/moondrop-widget/package" >&2
    echo "    $script_dir/../package" >&2
    echo "    /usr/share/moondrop-widget/package" >&2
    echo "    /usr/local/share/moondrop-widget/package" >&2
    echo "Set MOONDROP_PACKAGE_DIR to the directory that holds metadata.json." >&2
    exit 1
fi

KPACKAGE="kpackagetool6"
command -v "$KPACKAGE" >/dev/null 2>&1 || KPACKAGE="kpackagetool5"

if [ "${1:-install}" = "remove" ]; then
    "$KPACKAGE" --type Plasma/Applet --remove org.moondrop.control
    echo "Removed. Restart plasmashell or remove the widget from your panel."
    exit 0
fi

echo "Installing applet from $PKG"
if ! "$KPACKAGE" --type Plasma/Applet --upgrade "$PKG" 2>/dev/null; then
    "$KPACKAGE" --type Plasma/Applet --install "$PKG"
fi

cat <<'EOF'

Done. If the applet is already on your desktop/panel, run
    kquitapp6 plasmashell && kstart plasmashell
so Plasma picks up the new QML.  New widgets can be added right away via
"Add Widgets..." -> search for "Moondrop".
EOF
