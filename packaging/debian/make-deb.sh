#!/usr/bin/env bash
# Build a Debian/Ubuntu source package directory for Moondrop Control.
#
#   ./packaging/debian/make-deb.sh [output-directory]
#
# Produces a .deb plus the .dsc/.tar.xz/.changes triple that `dput`/`sbuild`
# would need, using dpkg-buildpackage.  Requires debhelper and the Qt 6 / KF6
# development packages; on a non-Debian host (e.g. Fedora) use the container
# route instead:
#
#   podman run --rm -v "$PWD:/src:ro" -v "$PWD/dist:/out:z" debian:trixie \
#       bash /src/packaging/debian/make-deb.sh /out
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/dist}"

if command -v dpkg-buildpackage >/dev/null 2>&1; then
    :
else
    echo "error: dpkg-buildpackage not found." >&2
    echo "On Debian/Ubuntu: sudo apt install devscripts debhelper dh-cmake" >&2
    echo "Elsewhere, run this script inside a debian container (see the header)." >&2
    exit 1
fi

VERSION="$(sed -n 's/.*"Version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/package/metadata.json" | head -1)"
NAME="moondrop-control"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/$NAME-$VERSION"
# ship a source tree without the local build products or the reference clones
tar -C "$ROOT" --exclude=./build --exclude=./dist --exclude=./ref-\* --exclude=./.git -cf - . \
    | tar -C "$WORK/$NAME-$VERSION" -xf -
cp -a "$ROOT/packaging/debian/debian" "$WORK/$NAME-$VERSION/debian"
chmod +x "$WORK/$NAME-$VERSION/debian/rules"

( cd "$WORK/$NAME-$VERSION" && dpkg-buildpackage -us -uc -b )

mkdir -p "$OUT"
find "$WORK" -maxdepth 1 -name '*.deb' -exec cp {} "$OUT/" \;
find "$WORK" -maxdepth 1 \( -name '*.dsc' -o -name '*.changes' -o -name '*.tar.*' \) -exec cp {} "$OUT/" \;

echo
echo "artifacts in $OUT:"
ls -1 "$OUT"
