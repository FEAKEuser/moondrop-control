#!/usr/bin/env bash
# Build the backend QML plugin against a deliberately old Qt, for the .plasmoid
# archive that goes to the KDE Store.
#
#   ./scripts/build-baseline-plugin.sh [container-image]
#
# Why: a Qt QML plugin records the Qt version it was built with, and the loader
# refuses it when the *host* Qt is older:
#
#     if ((plugin.minor > host.minor) || plugin.major != host.major) -> refuse
#
# (qtbase src/corelib/plugin/qlibrary.cpp).  So a plugin built on the newest Qt
# only runs on that Qt or newer, while a plugin built on an old one runs
# everywhere from there up.  Building the store archive on the oldest Qt you
# support - rather than on your development machine - is therefore what makes a
# single download work for everybody.  Verified on this project: a plugin built
# with Qt 6.8 loads in a Qt 6.11 plasmashell.
#
# The build runs in a container so the baseline Qt does not have to be installed
# on the development machine.  Fedora 40 is the default because it is the oldest
# image that has both Qt 6 (6.8) and the KF6 packages this project needs: Fedora
# 39 (Qt 6.5) predates KF6 entirely, so a KF6 build cannot be done there.  Pass
# a different image as the first argument if you have a leaner old-Qt base.
set -euo pipefail

IMAGE="${1:-docker.io/library/fedora:40}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/baseline/libmoondropplugin.so"

command -v podman >/dev/null 2>&1 || { echo "error: podman is required" >&2; exit 1; }

NAME="moondrop-baseline-$$"
cleanup() { podman rm -f "$NAME" >/dev/null 2>&1 || true; }
trap cleanup EXIT

echo "building the backend against $IMAGE"
podman run --name "$NAME" -d "$IMAGE" sleep 1800 >/dev/null

if ! podman exec "$NAME" bash -c '
    set -e
    dnf install -y -q cmake gcc-c++ ninja-build \
        qt6-qtbase-devel qt6-qtdeclarative-devel \
        extra-cmake-modules kf6-ki18n-devel >/dev/null 2>&1
    rpm -q qt6-qtbase kf6-ki18n
'; then
    echo "error: could not install the build dependencies in $IMAGE." >&2
    echo "The image needs Qt 6 *and* KF6 development packages; Fedora 39 and older" >&2
    echo "predate KF6, so use fedora:40 or newer." >&2
    exit 1
fi

# The plugin only needs src/ and the top level CMakeLists; the CLI and the test
# programs are irrelevant here and pulling them in would drag extra dependencies
# into the container.
podman exec "$NAME" mkdir -p /baseline/src
podman cp "$ROOT/CMakeLists.txt" "$NAME:/baseline/CMakeLists.txt"
podman cp "$ROOT/src/." "$NAME:/baseline/src/"
# the top level CMakeLists builds cli/ and the tests too; cut it down to the
# library so the baseline build needs nothing beyond the plugin's own deps
podman exec "$NAME" bash -c '
    set -e
    sed -i "s|^add_subdirectory(cli)$||" /baseline/CMakeLists.txt
    sed -i "/^install(DIRECTORY package\//d; /^install(PROGRAMS/d" /baseline/CMakeLists.txt
    cmake -S /baseline -B /baseline/build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build /baseline/build >/dev/null
'

mkdir -p "$(dirname "$OUT")"
podman cp "$NAME:/baseline/build/org/moondrop/backend/libmoondropplugin.so" "$OUT"

echo
echo "wrote $OUT"
echo "bundled plugin Qt version: $(podman exec "$NAME" rpm -q --qf '%{VERSION}' qt6-qtbase)"
echo
echo "Package it with:  MOONDROP_BACKEND_SO=$OUT ./scripts/make-plasmoid.sh"
