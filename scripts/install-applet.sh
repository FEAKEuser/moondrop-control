#!/usr/bin/env bash
# Install (or upgrade) the Moondrop Control Plasma applet for the current user.
#
#   ./install-applet.sh          install / upgrade
#   ./install-applet.sh remove   uninstall
set -euo pipefail

PKG="${MOONDROP_PACKAGE_DIR:-}"
if [ -z "$PKG" ]; then
    if [ -f "/usr/share/moondrop-widget/package/metadata.json" ]; then
        PKG="/usr/share/moondrop-widget/package"
    else
        PKG="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/package"
    fi
fi

if [ ! -f "$PKG/metadata.json" ]; then
    echo "error: applet package not found at $PKG" >&2
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
