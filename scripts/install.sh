#!/usr/bin/env bash
# One line installer for Moondrop Control.
#
#   curl -fsSL https://raw.githubusercontent.com/FEAKEuser/moondrop-control/master/scripts/install.sh | bash
#
# What it does, in order of preference:
#   1. the distribution package manager, if moondrop-control is packaged for the
#      system in use (best: updates come with the system, no build needed);
#   2. otherwise build from source into ~/.local (no root, nothing outside the
#      home directory);
#   3. otherwise refuse, with the missing packages named.
#
# Options:
#   --from-source      skip the package manager and always build
#   --source-dir <d>   build from a local checkout instead of cloning (dev use)
#   --prefix <dir>     install prefix for a source build (default ~/.local)
#   --uninstall        remove a source install (does not touch distro packages)
#   --ref <git-ref>    build a specific tag/branch (default: the latest tag)
set -euo pipefail

REPO="FEAKEuser/moondrop-control"
GIT_URL="https://github.com/$REPO.git"
PREFIX="${HOME}/.local"
MODE="auto"
REF=""
SOURCE_DIR=""

say()  { printf '\033[1m%s\033[0m\n' "$*"; }
warn() { printf '\033[33m%s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Install Moondrop Control.

    curl -fsSL https://raw.githubusercontent.com/FEAKEuser/moondrop-control/master/scripts/install.sh | bash

The installer prefers the distribution package when one exists for this system,
and otherwise builds from source into ~/.local without needing root.

Options:
  --from-source      skip the package manager and always build
  --source-dir <d>   build from a local checkout instead of cloning (dev use)
  --prefix <dir>     install prefix for a source build (default ~/.local)
  --uninstall        remove a source install (does not touch distro packages)
  --ref <git-ref>    build a specific tag/branch (default: the latest tag)
  -h, --help         show this help
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --from-source) MODE="source" ;;
        --source-dir) SOURCE_DIR="${2:?--source-dir needs a directory}"; MODE="source"; shift ;;
        --prefix) PREFIX="${2:?--prefix needs a directory}"; shift ;;
        --ref) REF="${2:?--ref needs a git ref}"; shift ;;
        --uninstall) MODE="uninstall" ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# Uninstall: remove exactly what a source install put down.
# ---------------------------------------------------------------------------
if [ "$MODE" = "uninstall" ]; then
    command -v kpackagetool6 >/dev/null 2>&1 \
        && kpackagetool6 --type Plasma/Applet --remove org.moondrop.control >/dev/null 2>&1 || true
    rm -f "$PREFIX/bin/moondrop-cli" \
          "$PREFIX/bin/moondrop-widget-install-applet" \
          "$PREFIX/bin/moondrop-widget-uninstall"
    rm -rf "$PREFIX/share/moondrop-widget" \
           "$PREFIX/lib/qt6/qml/org/moondrop" \
           "$PREFIX/share/plasma/plasmoids/org.moondrop.control"
    say "Removed the source install from $PREFIX."
    echo "The configuration in ~/.config/moondrop-widget was left alone."
    exit 0
fi

# ---------------------------------------------------------------------------
# 1. distribution package
# ---------------------------------------------------------------------------
try_distro() {
    if command -v rpm >/dev/null 2>&1 && command -v dnf >/dev/null 2>&1; then
        if dnf -q list --available moondrop-control >/dev/null 2>&1; then
            say "Installing moondrop-control with dnf"
            sudo dnf install -y moondrop-control
            return 0
        fi
    fi
    if command -v pacman >/dev/null 2>&1; then
        # not in the official repositories; the AUR helper is the user's call
        if command -v yay >/dev/null 2>&1; then
            say "Installing moondrop-control from the AUR with yay"
            yay -S --noconfirm moondrop-control
            return 0
        elif command -v paru >/dev/null 2>&1; then
            say "Installing moondrop-control from the AUR with paru"
            paru -S --noconfirm moondrop-control
            return 0
        fi
    fi
    if command -v apt-get >/dev/null 2>&1; then
        if apt-cache show moondrop-control >/dev/null 2>&1; then
            say "Installing moondrop-control with apt"
            sudo apt-get install -y moondrop-control
            return 0
        fi
    fi
    return 1
}

if [ "$MODE" = "auto" ] && try_distro; then
    say "Done. Add the widget from the panel: right click -> Add Widgets -> Moondrop."
    exit 0
fi

# ---------------------------------------------------------------------------
# 2. source build
# ---------------------------------------------------------------------------
say "Building from source"

missing=()
for tool in cmake git zip; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
[ ${#missing[@]} -eq 0 ] || die "Missing tools: ${missing[*]}"

# Qt is not negotiable: the applet is a QML plugin, so the Qt development files
# have to be there.  The error messages of CMake for a missing Qt6 are cryptic
# enough to be worth checking up front.
if ! ( command -v qmake6 >/dev/null 2>&1 || command -v qtpaths6 >/dev/null 2>&1 \
       || pkg-config --exists Qt6Core 2>/dev/null ); then
    warn "Qt 6 development files were not found. Install them first, for example"
    warn "    Fedora:        sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel extra-cmake-modules kf6-ki18n-devel"
    warn "    Debian/Ubuntu: sudo apt install qt6-base-dev qt6-declarative-dev extra-cmake-modules libkf6i18n-dev"
    warn "    Arch:          sudo pacman -S qt6-base qt6-declarative extra-cmake-modules kf6-ki18n"
    die  "Qt 6 development files are required."
fi

SRC="$(mktemp -d)"
trap 'rm -rf "$SRC"' EXIT

say "Fetching the sources"
if [ -n "$SOURCE_DIR" ]; then
    [ -f "$SOURCE_DIR/CMakeLists.txt" ] || die "$SOURCE_DIR does not look like the source tree"
    cp -a "$SOURCE_DIR" "$SRC/src"
    rm -rf "$SRC/src/build" "$SRC/src/dist"
elif [ -n "$REF" ]; then
    git clone --depth 1 --branch "$REF" "$GIT_URL" "$SRC/src"
else
    # the latest tag, so a plain install gets a release rather than a work in
    # progress; fall back to the default branch when there is no tag yet
    if git ls-remote --tags --refs "$GIT_URL" 2>/dev/null | grep -q 'refs/tags/v'; then
        latest="$(git ls-remote --tags --refs "$GIT_URL" \
                  | sed -n 's#.*refs/tags/v##p' | sort -V | tail -1)"
        git clone --depth 1 --branch "v$latest" "$GIT_URL" "$SRC/src"
    else
        git clone --depth 1 "$GIT_URL" "$SRC/src"
    fi
fi

say "Configuring"
# The build tree stays in the source checkout because make-plasmoid.sh picks the
# plugin up from there.
cmake -S "$SRC/src" -B "$SRC/src/build" -DCMAKE_BUILD_TYPE=Release >/dev/null

say "Building (this takes a minute)"
cmake --build "$SRC/src/build" -j "$(nproc 2>/dev/null || echo 2)" >/dev/null

# The applet is installed as a *self contained* package: the compiled backend
# plugin is copied inside it and the pages reach it by a relative import, so no
# root, no system Qt import directory and no QML_IMPORT_PATH are involved.  This
# is the same layout the KDE Store package uses, and it is also what makes one
# download work across Qt versions: the plugin here is built against the Qt that
# is actually running the desktop.
say "Packaging the applet"
PLASMOID="$(cd "$SRC/src" && ./scripts/make-plasmoid.sh "$SRC/out" | sed -n 's/^wrote //p' | head -1)"
[ -n "$PLASMOID" ] && [ -f "$PLASMOID" ] || die "could not build the applet package"

if command -v kpackagetool6 >/dev/null 2>&1; then
    say "Installing the applet for the current user"
    kpackagetool6 --type Plasma/Applet --upgrade "$PLASMOID" >/dev/null 2>&1 \
        || kpackagetool6 --type Plasma/Applet --install "$PLASMOID" >/dev/null
else
    warn "kpackagetool6 was not found, so the applet was not installed."
    warn "The package is at $PLASMOID; install it later with"
    warn "    kpackagetool6 --type Plasma/Applet --install $PLASMOID"
fi

# The command line tool is a normal binary; it needs no plugin and no root.
if [ -x "$SRC/src/build/cli/moondrop-cli" ]; then
    say "Installing moondrop-cli into $PREFIX/bin"
    mkdir -p "$PREFIX/bin"
    install -m755 "$SRC/src/build/cli/moondrop-cli" "$PREFIX/bin/moondrop-cli"
fi

say "Done."
cat <<EOF

Add the widget from the panel: right click -> "Add Widgets..." -> search for
"Moondrop".  Then pick your headphone in the widget's settings; the widget
connects on its own from then on.

Nothing was installed outside your home directory and no root was needed.  To
remove it again:

    curl -fsSL https://raw.githubusercontent.com/$REPO/master/scripts/install.sh | bash -s -- --uninstall
EOF
