#!/usr/bin/env bash
# Remove the Moondrop Control applet and (optionally) the QML plugin.
set -euo pipefail

KPACKAGE="kpackagetool6"
command -v "$KPACKAGE" >/dev/null 2>&1 || KPACKAGE="kpackagetool5"

"$KPACKAGE" --type Plasma/Applet --remove org.moondrop.control || true

echo "The applet was removed. To remove the QML plugin and the command line tool:"
echo "    sudo rm -rf \$(qtpaths6 --query QT_INSTALL_QML)/org/moondrop"
echo "    sudo rm -f /usr/bin/moondrop-cli /usr/bin/moondrop-widget-install-applet"
echo "    rm -f ~/.config/moondrop-widget/config.ini"
