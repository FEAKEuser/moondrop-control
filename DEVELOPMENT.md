# Development notes

## Layout

```
src/gaia.{h,cpp}            GAIA frame encode/decode + incremental stream parser
src/rfcommclient.{h,cpp}    async RFCOMM client on AF_BLUETOOTH (no libbluetooth)
src/moondropdevice.{h,cpp}  device model: serial request queue, probing, state
src/devicediscovery.{h,cpp} BlueZ D-Bus device list for the settings page
src/plugin.cpp              QML plugin: singleton `Moondrop`, type `DeviceDiscovery`
cli/main.cpp                moondrop-cli
tools/preview.cpp           render one QML page to a PNG (offscreen)
tools/gaia.py               standalone python GAIA client for experiments
package/                    the Plasma applet
```

## Working on the UI without disturbing the desktop

```bash
cmake --build build -j$(nproc)
MOONDROP_PREVIEW_CONNECT=1 QT_QPA_PLATFORM=offscreen \
  ./build/cli/preview package/contents/ui/EqPage.qml /tmp/eq.png 430 660
```

The harness imports the QML plugin from `build/` (so a freshly built plugin wins
over an installed one), which gives the pages a real `Moondrop` singleton.
`MOONDROP_PREVIEW_NOCONNECT=1` keeps it disconnected, `MOONDROP_DEBUG=1` prints
the protocol traffic, `MOONDROP_LOCALE_DIR=<dir>` picks the translation catalogue.

For small pieces of UI there are focused checks that need no hardware at all,
for example `tests/output-gain.qml` (the gain label mapping).  They are plain QML
files rendered through the same tool:

```bash
./build/cli/preview tests/output-gain.qml /tmp/gain.png 460 560
```

Note that the offscreen platform plugin used by the harness does not draw the
checked state of Plasma buttons, so such checks print the result as text too.

Two hardware-free test programs are worth running before every build you install:

```bash
./build/cli/moondrop-selftest    # protocol codec: framing, PEQ payload, profiles
./build/cli/moondrop-conncheck   # connect / queue / notification behaviour
```

`moondrop-conncheck` runs the real `MoondropDevice` against the fake headset and
covers the failure modes that actually bit us:

* a request queued while there is nothing to talk to keeps `busy` true forever
  (the UI then refuses every action - it looks like "cannot connect any more"),
  so requests are dropped when disconnected and an explicit connect always
  starts from a clean queue;
* reading a feature in reaction to a notification can make the device emit
  another notification.  Without a rate limit this ping-pongs forever, so
  notification driven refreshes are debounced (see
  `MoondropDevice::notificationRefreshAllowed`) - the test asserts the device
  goes idle and stays idle with no frames on the wire.

To try the whole applet without installing it:

```bash
QML_IMPORT_PATH=$PWD/build plasmawindowed org.moondrop.control
```

(the applet package itself still has to be installed with `kpackagetool6`, see
`scripts/install-applet.sh`) and after editing QML either restart plasmashell or
re-run `kpackagetool6 --upgrade package`.

## Talking to the hardware

```bash
MOONDROP_DEBUG=1 ./build/cli/moondrop-cli info          # shows every frame
./build/cli/moondrop-cli raw 8 3                        # read the ANC mode
./build/cli/moondrop-cli raw 8 4 02                     # set ANC
python3 tools/gaia.py enum                              # sweep read-only commands
```

Protocol details, including how the firmware behaves under load, are in
`docs/PROTOCOL.md` — read the "timing" section before adding new commands.

## Vector artwork

`package/contents/ui/AppletArtwork.qml` holds the applet's SVGs **as QML string
templates** (with `%1` for the colour), not as separate `.svg` files.  Two reasons:

* `Image.source` resolves a relative URL against the *process working directory*,
  not against the QML file (`Qt.resolvedUrl(..., Component)` does not fix this
  either), so a separate file breaks as soon as the package is installed
  elsewhere.  A `data:image/svg+xml,...` URL always works.
* the colour can follow the Plasma theme: the templates are filled in with
  `Kirigami.Theme.textColor` when the URL is built.

The battery is drawn **upright** on purpose: in a horizontal panel width is the
scarce axis, so the gauge is 9 px wide instead of the 17 px a lying battery (or
the "90 %" text it replaced) needs.  Its inner area is x 2.1..9.9 / y 5.1..22.9 of
the 12x26 viewBox; `BatteryIndicator.qml` maps the charge to that area and lets the
fill grow upwards.

The test files in `tests/` are copied into the package directory before rendering
(`scripts/run-ui-checks.sh`) because a relative directory import only resolves
against the importing file - the singleton in `contents/ui/qmldir` is not found
from outside the package.

## The fake headset

`tools/fakeheadset.h` implements the `Transport` interface with canned answers
modelled on a real EDGE (plus Pudding/Robin variants), so the UI and the state
machine can be exercised without hardware:

```bash
MOONDROP_PREVIEW_FAKE=edge    ./build/cli/preview package/contents/ui/EqPage.qml  /tmp/eq.png 520 800
MOONDROP_PREVIEW_FAKE=pudding ./build/cli/preview package/contents/ui/InfoPage.qml /tmp/p.png 430 500
MOONDROP_PREVIEW_FAKE=robin   ./build/cli/preview package/contents/ui/AncPage.qml  /tmp/a.png 430 500
```

The command line tool takes the same switch (`MOONDROP_FAKE=edge`), which makes the
write paths testable without hardware.  Note that each run is a new process, so the
fake starts from its initial state every time - a set/read pair has to happen inside
one invocation, e.g. `MOONDROP_FAKE=edge MOONDROP_DEBUG=1 ./build/cli/moondrop-cli anc on`
shows the write and the verification read that follows it.

This is also the only way to look at the UI while a widget in plasmashell holds the
real RFCOMM link - a headphone serves exactly one connection at a time.

## Device profiles

`src/deviceprofile.cpp` lists the model quirks (ANC command family, battery layout,
gain order, PEQ support). To add a model:

1. Append a `DeviceProfile` entry (name patterns are matched against the model name
   reported by `BASIC/GET_VARIANT`).
2. Only set `verified = true` once the model was actually measured; the UI shows
   that flag to the user.
3. Add a case to `tests/protocol.cpp::testProfiles()` and a variant to
   `FakeHeadset` if you have captured frames.

## Translations

User visible strings are collected in `po/<lang>.po` and compiled into the applet
package (`package/contents/locale/...`), which is where Plasma looks them up:

```bash
./scripts/update-translations.sh    # after editing po/*.po
./scripts/install-applet.sh         # copy the package (with the .mo) into place
```

* QML: use the `i18n()` / `i18nc()` functions as usual.
* C++: use `moondropTr()` from `src/i18n.h` - it looks the string up in
  `plasma_applet_org.moondrop.control`.  Do **not** use `QObject::tr()`: it would
  put the string into a different catalogue namespace, and Qt 6's own translator
  cannot read gettext `.mo` files anyway (KI18n can, which is why that dependency
  exists).

`tests/` also holds the QML checks described above.

Both test programs (and the preview/CLI tools in fake mode) point `MOONDROP_CONFIG`
at a scratch file via `MOONDROP_CONFIG`, so they never read or write the user's
configuration.

## Safety nets worth knowing about

* **Model matching.** `DeviceProfile::namePatterns` are substrings, so a generic
  one is dangerous: a bare `EDGE` used to make "Samsung EDGE 5G" look like a
  MOONDROP EDGE and receive headphones-only commands.  Short names belong in
  `exactNames` (whole-name comparison).  `testProfiles()` in
  `tests/protocol.cpp` covers the foreign-name cases.
* **Offline gate.** `tryNextChannel()` asks `BlueZWatcher` first: if Bluetooth
  reports the headphone as disconnected there is no point opening the control
  channel, so it waits for the watcher instead (and the UI says so).
* **EQ restore point.** Every curve the *headphone* reports is stored
  (`eq/deviceBackup` in the config), so an accidental flatten can be undone from
  the Tuning page.  Writing a curve snapshots the previous one first.
* **Battery aggregation.** For earbuds the panel shows the **weaker** bud, since
  showing the better one hides an almost empty side.

## Adding a new command

1. Add the command id to the anonymous namespace in `src/moondropdevice.cpp`.
2. Add a parse branch in `handleResponse()` and a `Q_INVOKABLE` setter/getter.
3. Gate the query in `refreshDetails()` with `hasFeature(...)`.
4. For writes, prefer `expectReply = false` plus a queued verification read
   (`enqueue()`), and call `prepareUserAction()` first so a background poll
   cannot delay it.
5. Expose the value as a `Q_PROPERTY` and use it from QML.

## Repository layout note

The `ref-*/` directories are *other people's repositories*, cloned locally for
reading during development.  They are in `.gitignore` and must never be committed -
they carry their own licences and histories.  The same goes for `build/`.

If you re-clone those sources to study a protocol, keep them outside the working
tree (or under `ref-`, which is ignored).
