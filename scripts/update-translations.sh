#!/usr/bin/env bash
# Compile the gettext catalogues in po/ into the applet package.
#
#   ./scripts/update-translations.sh
#
# Result:
#   package/contents/locale/<lang>/LC_MESSAGES/plasma_applet_org.moondrop.control.mo
#
# Plasma looks up the applet's strings in that file (domain
# "plasma_applet_<applet id>"), so the package has to be re-installed after
# running this (scripts/install-applet.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOMAIN="plasma_applet_org.moondrop.control"
PO_DIR="$ROOT/po"
OUT_DIR="$ROOT/package/contents/locale"

if ! command -v msgfmt >/dev/null 2>&1; then
    echo "error: msgfmt (gettext) is required. On Fedora: sudo dnf install gettext" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Check that every user visible string has a translation (and that the catalogue
# has no leftovers).  Keeps po/<lang>.po in sync with the sources.
# ---------------------------------------------------------------------------
python3 - "$ROOT" <<'PYCHECK'
import glob
import os
import re
import sys

root = sys.argv[1]
sources = set()

for path in glob.glob(os.path.join(root, "package/contents/**/*.qml"), recursive=True):
    text = open(path, encoding="utf-8").read()
    sources.update(m.group(2) for m in re.finditer(r'\bi18n(c)?\(\s*"((?:[^"\\]|\\.)*)"', text))

for path in glob.glob(os.path.join(root, "src/*.cpp")):
    text = open(path, encoding="utf-8").read()
    # moondropTr("...") joins adjacent string literals into one message
    for match in re.finditer(r'moondropTr\(((?:\s*"(?:[^"\\]|\\.)*")+)', text):
        literals = re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1))
        sources.add("".join(literals))

def msgids(path):
    """Collect the msgid strings of a .po file, joining wrapped continuations."""
    entries = set()
    current = None  # None = not inside an entry, "" = collecting an id
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("msgid "):
            current = line[len("msgid "):]
        elif line.startswith('"') and current is not None:
            current += line
        elif line.startswith("msgstr"):
            if current is not None:
                text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', current))
                if text:
                    entries.add(text)
            current = None
            continue
        elif line.strip() == "" or line.startswith("#"):
            if current is not None:
                text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', current))
                if text:
                    entries.add(text)
                current = None
    if current is not None:
        text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', current))
        if text:
            entries.add(text)
    return entries


failed = False
for po in sorted(glob.glob(os.path.join(root, "po/*.po"))):
    have = msgids(po)
    missing = sorted(sources - have)
    unused = sorted(have - sources)
    if missing:
        print(f"{os.path.relpath(po, root)}: {len(missing)} string(s) without translation:")
        for entry in missing:
            print(f"    {entry!r}")
        failed = True
    if unused:
        print(f"{os.path.relpath(po, root)}: {len(unused)} obsolete entry/entries:")
        for entry in unused:
            print(f"    {entry!r}")
        failed = True
    if not missing and not unused:
        print(f"{os.path.relpath(po, root)}: complete")

sys.exit(1 if failed else 0)
PYCHECK

shopt -s nullglob
found=0
for po in "$PO_DIR"/*.po; do
    lang="$(basename "$po" .po)"
    mkdir -p "$OUT_DIR/$lang/LC_MESSAGES"
    msgfmt --check-format --statistics -o "$OUT_DIR/$lang/LC_MESSAGES/$DOMAIN.mo" "$po"
    echo "wrote $OUT_DIR/$lang/LC_MESSAGES/$DOMAIN.mo"
    found=1
done

if [ "$found" -eq 0 ]; then
    echo "no .po files in $PO_DIR" >&2
    exit 1
fi
