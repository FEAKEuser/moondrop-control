#!/usr/bin/env bash
# Build a self-contained .plasmoid archive for the KDE Store / "Get New Widgets".
#
#   ./scripts/make-plasmoid.sh [output-directory]
#
# Why this is needed: the applet's backend is a C++ QML plugin, not QML, so the
# package a user downloads has to carry the compiled plugin inside itself.  The
# store delivers applet packages as a single archive, and the KNS configuration
# that Plasma uses for widgets explicitly allows that ("ContentWarning=Executables"
# in /usr/share/knsrcfiles/plasmoids.knsrc), so a binary inside the package is
# expected rather than exceptional.
#
# Layout produced (the plugin lives inside the applet package and the QML pages
# reach it through a relative directory import):
#
#   metadata.json
#   contents/ui/main.qml ... import "backend"
#   contents/ui/backend/libmoondropplugin.so
#   contents/ui/backend/qmldir
#   contents/locale/...
#
# A relative directory import is used on purpose.  A module URI import
# (`import org.moondrop.backend 1.0`) resolves against Qt's global import path,
# which a package delivered by the store cannot write to; a directory import is
# resolved against the QML file itself, so it works from wherever the package is
# unpacked.  This was verified against a real plasmashell: the plugin's
# registerTypes() is called with the package's own copy.
#
# The archive is a plain zip; Plasma's KNS entry (Uncompress=kpackage) installs
# it with kpackagetool6, and the same file can be installed by hand:
#
#   kpackagetool6 --type Plasma/Applet --install moondrop-control-0.1.0.plasmoid
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-$ROOT/dist}"

# Which plugin goes into the archive, in order of preference:
#
#   1. MOONDROP_BACKEND_SO           - explicit, always wins
#   2. build/baseline/...            - the old-Qt build, when one has been made
#   3. build/org/moondrop/backend/... - whatever was built locally
#
# The baseline build is preferred over the local one on purpose: a plugin built
# against a *newer* Qt is refused by an older host ("uses incompatible Qt
# library"), so the old build is the safer default and picking it up automatically
# makes it hard to publish an archive that only runs on this machine.
BASELINE_SO="$ROOT/build/baseline/libmoondropplugin.so"
if [ -n "${MOONDROP_BACKEND_SO:-}" ]; then
    BACKEND_SO="$MOONDROP_BACKEND_SO"
elif [ -f "$BASELINE_SO" ]; then
    BACKEND_SO="$BASELINE_SO"
else
    BACKEND_SO="$ROOT/build/org/moondrop/backend/libmoondropplugin.so"
fi
if [ ! -f "$BACKEND_SO" ]; then
    echo "error: $BACKEND_SO not found." >&2
    echo "Build the project first (cmake --build build), or set MOONDROP_BACKEND_SO." >&2
    exit 1
fi

VERSION="$(sed -n 's/.*"Version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/package/metadata.json" | head -1)"
ID="$(sed -n 's/.*"Id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/package/metadata.json" | head -1)"
[ -n "$VERSION" ] && [ -n "$ID" ] || { echo "error: could not read Id/Version from package/metadata.json" >&2; exit 1; }

STAGE="$ROOT/build/plasmoid/$ID"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$ROOT/package/." "$STAGE/"

# The backend plugin travels inside the package.
#
# The bundled qmldir uses a *package private* module name.  It must not claim
# the public `org.moondrop.backend`: when both copies are present (a user who
# installed the distribution package and then the store package) the globally
# installed module owns the URI, and the bundled copy is then refused with
#     Cannot install singleton type 'Moondrop' into protected module
# which breaks every page of the store package.  Omitting the module line
# altogether avoids the clash too, but Qt then logs
#     Module '...' does not contain a module identifier directive
# on every start, so a private name is used instead.  Both were measured
# against a plasmashell that had the global module installed.
mkdir -p "$STAGE/contents/ui/backend"
cp "$BACKEND_SO" "$STAGE/contents/ui/backend/libmoondropplugin.so"
cat > "$STAGE/contents/ui/backend/qmldir" <<'EOF'
module org.moondrop.backend.bundled
plugin moondropplugin
EOF

# Point the pages at the bundled copy instead of the globally installed module.
while IFS= read -r -d '' qml; do
    sed -i 's|^import org\.moondrop\.backend 1\.0$|import "backend"|' "$qml"
done < <(find "$STAGE/contents/ui" -name '*.qml' -print0)

if grep -rq "import org\.moondrop\.backend" "$STAGE/contents/ui"; then
    echo "error: a page still imports the global module:" >&2
    grep -rn "import org\.moondrop\.backend" "$STAGE/contents/ui" >&2
    exit 1
fi

# Airplane-safety: the archive must not pick up local build leftovers.
rm -f "$STAGE/contents/ui/backend/libmoondropplugin.so.debug"

mkdir -p "$OUT_DIR"
ARCHIVE="$OUT_DIR/$ID-$VERSION.plasmoid"
rm -f "$ARCHIVE"
( cd "$STAGE" && zip -q -r -X "$ARCHIVE" . -x '.*' )

# kpackagetool6 is the authority on whether the package is installable; use it
# rather than trusting the zip.
if command -v kpackagetool6 >/dev/null 2>&1; then
    SCRATCH="$(mktemp -d)"
    trap 'rm -rf "$SCRATCH"' EXIT
    kpackagetool6 --type Plasma/Applet -p "$SCRATCH" --install "$ARCHIVE" >/dev/null
    echo "verified: kpackagetool6 installed the archive into a scratch root"
fi

echo
echo "wrote $ARCHIVE"
echo "staged package: $STAGE"

# State the plugin's Qt version concretely rather than just repeating a rule: the
# loader refuses a plugin whose minor version is newer than the host's, so an
# archive built on this machine only installs on Qt >= the version below.
PLUGIN_QT=""
for tool in /usr/lib64/qt6/bin/qtplugininfo /usr/lib/qt6/bin/qtplugininfo; do
    [ -x "$tool" ] && PLUGIN_QT="$("$tool" "$BACKEND_SO" 2>/dev/null | head -1)" && break
done
if [ -n "$PLUGIN_QT" ]; then
    echo "bundled plugin: $PLUGIN_QT"
fi
echo
echo "Reminder: Qt refuses a plugin whose minor version is newer than the host's,"
echo "so this archive only runs on that Qt or newer.  For a release, build the"
echo "plugin on the oldest Qt you support (scripts/build-baseline-plugin.sh) and"
echo "pass it via MOONDROP_BACKEND_SO - see DEVELOPMENT.md, 'The Qt version gate'."
