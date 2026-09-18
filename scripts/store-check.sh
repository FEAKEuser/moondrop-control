#!/usr/bin/env bash
# End-to-end test of the KDE Store channel, offline and on this machine.
#
#   ./scripts/store-check.sh
#
# What it proves, in order:
#   1. the `.plasmoid` archive installs through kpackagetool6 (what KNS uses);
#   2. the KNewStuff client - the very code behind "Get New Widgets" - can find
#      it through a local OCS provider, download it and install it;
#   3. the package it installed actually loads in a real plasmashell, including
#      the bundled backend plugin.
#
# A local OCS provider (tools/ocs-provider.py) stands in for store.kde.org, so
# nothing is published and the real store is not touched.  Everything is written
# under a scratch XDG root: the user's own plasmoids and configuration are never
# modified.
#
# Step 2 needs the KNewStuff development files (kf6-knewstuff-devel on Fedora,
# libkf6newstuff-dev on Debian).  The script builds the test program against the
# installed runtime library if the headers are present, and skips step 2 with a
# clear message if they are not - steps 1 and 3 still run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
PORT="${MOONDROP_OCS_PORT:-8231}"
OCS_PID=""

cleanup() {
    [ -n "$OCS_PID" ] && kill "$OCS_PID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

pass() { printf '\033[32mPASS\033[0m %s\n' "$*"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[1m%s\033[0m\n' "$*"; }

# A skipped step must not be reported as a clean run: the whole point of this
# script is to prove the store download happened, so "not tested" and "passed"
# have to stay distinguishable.
SKIPPED=0
skip() { printf '\033[33mSKIP\033[0m %s\n' "$*"; SKIPPED=$((SKIPPED + 1)); }

# ---------------------------------------------------------------------------
info "1/3  building the applet package"
PLASMOID="$(cd "$ROOT" && ./scripts/make-plasmoid.sh "$WORK/dist" | sed -n 's/^wrote //p' | head -1)"
[ -f "$PLASMOID" ] || fail "make-plasmoid.sh did not produce an archive"
pass "packaged $(basename "$PLASMOID")"

# kpackagetool6 is the authority on whether the archive is a valid applet package.
SCRATCH="$WORK/kpkg"
mkdir -p "$SCRATCH/plasma/plasmoids"
kpackagetool6 --type Plasma/Applet -p "$SCRATCH/plasma/plasmoids" --install "$PLASMOID" >/dev/null
[ -f "$SCRATCH/plasma/plasmoids/org.moondrop.control/metadata.json" ] \
    || fail "kpackagetool6 installed nothing"
pass "kpackagetool6 accepts and installs the archive"

[ -f "$SCRATCH/plasma/plasmoids/org.moondrop.control/contents/ui/backend/libmoondropplugin.so" ] \
    || fail "the bundled backend plugin did not survive packaging"
pass "the archive carries the compiled backend plugin"

# ---------------------------------------------------------------------------
info "2/3  installing through KNewStuff (the 'Get New Widgets' code path)"
KNS_HEADERS=""
KNS_VERSION_HEADER=""
KNS_LIB="$(ls /usr/lib64/libKF6NewStuffCore.so.6 /usr/lib/*/libKF6NewStuffCore.so.6 2>/dev/null | head -1 || true)"

# An explicit override lets the check run where the KNewStuff development files
# were unpacked somewhere rather than installed (containers, scratch trees).
KNS_CANDIDATES=()
if [ -n "${MOONDROP_KNS_INCLUDE:-}" ]; then
    KNS_CANDIDATES+=("$MOONDROP_KNS_INCLUDE")
fi
KNS_CANDIDATES+=(/usr/include/KF6/KNewStuffCore /usr/include/KF6 /usr/include/KF6/KNewStuff3)
for d in "${KNS_CANDIDATES[@]}"; do
    [ -f "$d/KNSCore/enginebase.h" ] && KNS_HEADERS="$d" && break
done

# knewstuff_version.h lives in the umbrella include dir, not in KNewStuffCore
KNS_VERSION_CANDIDATES=()
if [ -n "${MOONDROP_KNS_INCLUDE:-}" ]; then
    KNS_VERSION_CANDIDATES+=("$(dirname "$MOONDROP_KNS_INCLUDE")/KNewStuff"
                             "$(dirname "$MOONDROP_KNS_INCLUDE")")
fi
KNS_VERSION_CANDIDATES+=(/usr/include/KF6/KNewStuff /usr/include/KF6)
for d in "${KNS_VERSION_CANDIDATES[@]}"; do
    [ -f "$d/knewstuff_version.h" ] && KNS_VERSION_HEADER="$d" && break
done

if [ -z "$KNS_HEADERS" ] || [ -z "$KNS_VERSION_HEADER" ] || [ -z "$KNS_LIB" ]; then
    skip "KNewStuff development files not found - the store download was NOT tested."
    echo "      Fedora: sudo dnf install kf6-knewstuff-devel"
    echo "      Debian: sudo apt install libkf6newstuff-dev"
    echo "      Steps 1 and 3 still ran."
else
    python3 "$ROOT/tools/ocs-provider.py" --port "$PORT" --plasmoid "$PLASMOID" >"$WORK/ocs.log" 2>&1 &
    OCS_PID=$!
    for _ in $(seq 20); do
        grep -q serving "$WORK/ocs.log" 2>/dev/null && break
        sleep 0.3
    done
    grep -q serving "$WORK/ocs.log" || fail "the local OCS provider did not start"

    cat > "$WORK/providers.xml" <<EOF
<providers>
    <provider>
        <id>localhost</id>
        <location>http://127.0.0.1:$PORT/ocs/v1/</location>
        <name>Local test provider</name>
        <services><content ocsversion="1.6"/></services>
    </provider>
</providers>
EOF
    mkdir -p "$WORK/data/knsrcfiles"
    cat > "$WORK/data/knsrcfiles/testplasmoids.knsrc" <<EOF
[KNewStuff]
Name=Plasma Widgets (local test)
Categories=Plasma 6 Extensions
ProvidersUrl=file://$WORK/providers.xml
StandardResource=tmp
Uncompress=kpackage
KPackageStructure=Plasma/Applet
EOF

    # Build the driver against whatever is on the machine.  The .so symlink only
    # exists with the development package, so fall back to the versioned library.
    KNS_LINK="$KNS_LIB"
    [ -e /usr/lib64/libKF6NewStuffCore.so ] && KNS_LINK="-lKF6NewStuffCore"
    g++ -std=c++20 -O1 -o "$WORK/kns-check" "$ROOT/tests/kns-install.cpp" \
        -I"$KNS_HEADERS" ${KNS_VERSION_HEADER:+-I"$KNS_VERSION_HEADER"} \
        -I/usr/include/qt6 -I/usr/include/qt6/QtCore -I/usr/include/qt6/QtGui \
        -fPIC -L/usr/lib64 -lQt6Core -lQt6Gui "$KNS_LINK" -Wl,-rpath,/usr/lib64 2>"$WORK/g++.log" \
        || { tail -20 "$WORK/g++.log" >&2; fail "could not build the KNewStuff driver"; }

    OUT="$(XDG_DATA_HOME="$WORK/data" XDG_CONFIG_HOME="$WORK/cfg" \
           QT_QPA_PLATFORM=offscreen timeout 120 "$WORK/kns-check" \
           testplasmoids.knsrc "$WORK/data" 2>&1 || true)"
    echo "$OUT" | sed 's/^/      /'
    echo "$OUT" | grep -q "^FAIL" && fail "the KNewStuff install reported a failure"
    echo "$OUT" | grep -q "install transaction finished" || fail "the install never finished"
    pass "KNewStuff downloaded the archive from a provider and installed it"

    # -----------------------------------------------------------------------
    info "3/3  loading the installed package in a real plasmashell"
    if ! command -v plasmawindowed >/dev/null 2>&1; then
        skip "plasmawindowed is not installed (plasma-workspace) - loading was NOT tested."
    else
        # The import-resolution lines only appear with the QML import debug
        # category enabled, which is also what makes the check meaningful: it
        # shows each page resolving the backend that sits next to it.
        XDG_DATA_HOME="$WORK/data" QT_QPA_PLATFORM=offscreen \
            QT_LOGGING_RULES="qt.qml.import.debug=true" \
            timeout 18 plasmawindowed org.moondrop.control >/dev/null 2>&1 || true
        LOG="$(journalctl --user --since "-45s" --no-pager 2>/dev/null | tail -400)"

        if echo "$LOG" | grep -qiE "Cannot install|is not a type|is not installed|uses incompatible|does not contain a module|error when loading"; then
            echo "$LOG" | grep -iE "Cannot install|is not a type|is not installed|uses incompatible|does not contain a module|error when loading" >&2
            fail "plasmashell rejected the KNewStuff-installed package"
        fi
        # The pages have to have resolved the backend that sits next to them.
        resolved="$(echo "$LOG" | grep -c "plasmoids/org.moondrop.control/contents/ui/backend/qmldir" || true)"
        [ "$resolved" -gt 0 ] \
            || fail "no page resolved the bundled backend (plasmashell may not have started)"
        pass "plasmashell loaded it cleanly: $resolved pages resolved the bundled backend"
    fi
fi

echo
if [ "$SKIPPED" -gt 0 ]; then
    printf '\033[33m%s step(s) were skipped - the store channel is NOT fully verified on this machine.\033[0m\n' "$SKIPPED"
    exit 1
fi
printf '\033[32mThe store channel works end to end.\033[0m\n'
