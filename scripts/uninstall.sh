#!/usr/bin/env bash
# Remove the Moondrop Control applet and (optionally) the QML plugin.
#
# The QML plugin lives in Qt's own import directory, which is not necessarily
# under the install prefix - ask Qt where that is instead of guessing.
set -euo pipefail

KPACKAGE="kpackagetool6"
command -v "$KPACKAGE" >/dev/null 2>&1 || KPACKAGE="kpackagetool5"

"$KPACKAGE" --type Plasma/Applet --remove org.moondrop.control || true

QML_DIR=""
for tool in qtpaths6 qtpaths; do
    if command -v "$tool" >/dev/null 2>&1; then
        QML_DIR="$("$tool" --query QT_INSTALL_QML 2>/dev/null || true)"
        [ -n "$QML_DIR" ] && break
    fi
done

# the prefix this script was installed under, for the remaining files
script_path="${BASH_SOURCE[0]}"
if command -v readlink >/dev/null 2>&1; then
    resolved="$(readlink -f "$script_path" 2>/dev/null || true)"
    [ -n "$resolved" ] && script_path="$resolved"
fi
script_dir="$(cd "$(dirname "$script_path")" && pwd)"
prefix="$(dirname "$script_dir")"

echo
echo "The applet was removed. To remove the QML plugin, the command line tool"
echo "and the installed package directory:"
echo
if [ -n "$QML_DIR" ]; then
    echo "    sudo rm -rf $QML_DIR/org/moondrop"
else
    echo "    sudo rm -rf \$(qtpaths6 --query QT_INSTALL_QML)/org/moondrop"
fi
echo "    sudo rm -f $script_dir/moondrop-cli $script_dir/moondrop-widget-install-applet $script_dir/moondrop-widget-uninstall"
echo "    sudo rm -rf $prefix/share/moondrop-widget"
echo "    rm -f ~/.config/moondrop-widget/config.ini"
