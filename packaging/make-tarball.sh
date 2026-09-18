#!/usr/bin/env bash
# Build the source tarball that the distribution packages point at.
#
#   ./packaging/make-tarball.sh [output-directory]
#
# Produces moondrop-control-<version>.tar.gz with a single top level directory,
# which is what both the Fedora spec (%autosetup -n) and the PKGBUILD expect.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/dist}"

VERSION="$(sed -n 's/.*"Version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/package/metadata.json" | head -1)"
NAME="moondrop-control"
[ -n "$VERSION" ] || { echo "error: no Version in package/metadata.json" >&2; exit 1; }

mkdir -p "$OUT"
TARBALL="$OUT/$NAME-$VERSION.tar.gz"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PREFIX="$STAGE/$NAME-$VERSION"
mkdir -p "$PREFIX"

# Everything tracked in git, minus the local-only directories.  Using git keeps
# the reference clones and build products out without a growing exclude list.
#
# A release tarball has to match a commit, so a dirty tree is refused rather than
# quietly packaged: a tarball built from the working copy is not reproducible and
# would not match the tag it claims to be.
if git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    if [ -n "$(git -C "$ROOT" status --porcelain)" ]; then
        echo "error: the working tree has uncommitted changes." >&2
        echo "Commit them first - a source tarball has to correspond to a commit." >&2
        git -C "$ROOT" status --short >&2
        exit 1
    fi
    git -C "$ROOT" archive --format=tar --prefix="$NAME-$VERSION/" HEAD | tar -C "$STAGE" -xf -
else
    echo "note: not a git checkout, copying the working tree" >&2
    tar -C "$ROOT" --exclude=./build --exclude=./dist --exclude=./ref-\* --exclude=./.git \
        --transform "s|^\./|$NAME-$VERSION/|" -cf - . | tar -C "$STAGE" -xf -
fi

[ -f "$PREFIX/CMakeLists.txt" ] || { echo "error: tarball has no CMakeLists.txt" >&2; exit 1; }

rm -f "$TARBALL"
# --sort=name and the fixed mtime make the tarball reproducible
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='@0' \
    -czf "$TARBALL" -C "$STAGE" "$NAME-$VERSION"

echo "wrote $TARBALL ($(du -h "$TARBALL" | cut -f1))"
