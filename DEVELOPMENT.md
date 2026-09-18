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

Six hardware-free test programs are worth running before every build you install:

```bash
./build/cli/moondrop-selftest      # protocol codec: framing, PEQ payload, profiles
./build/cli/moondrop-profile-check # model matching (incl. the foreign-name trap)
./build/cli/moondrop-conncheck     # connect / queue / notification behaviour
./build/cli/moondrop-scan-check    # channel scan, incl. a wrong channel that goes silent
./build/cli/moondrop-ebusy-check   # EBUSY: give up after the retry budget, with an error
./build/cli/moondrop-switch-check  # switching headphones: no info from the old one
./build/cli/moondrop-watchstate-check # BlueZ "Connected" parsing: not read as a disconnect
./build/cli/moondrop-reconnect-check # recovery without pressing "Retry now"
./build/cli/moondrop-stress        # churn + settle: no crash, and the scan still connects
```

Two of them need real hardware state and skip themselves otherwise:
`moondrop-failover-check <paired-but-offline-address>` and
`moondrop-reconnect-check`.  Two diagnostic helpers print what the applet reacts
to, which is how the switch behaviour was verified on a real device:

```bash
./build/cli/moondrop-hpwatch <address> <seconds>   # every BlueZ headphone event
./build/cli/moondrop-watchswitch <seconds>         # connection log while switching pairs
```

`moondrop-scan-check` also runs against real hardware (`moondrop-scan-check <addr>`,
and `--fresh` to exercise the no-address-configured path).

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
   that flag to the user.  As of today exactly two profiles carry it - `edge`
   (firmware 1.4.0) and `nekocake` (firmware 1.0.0) - and README.md's "measured
   scope" table must be updated in the same change when a third one is added.
3. Add a case to `tests/protocol.cpp::testProfiles()` and to
   `tests/prof.cpp` (which also covers the foreign-name trap), and a variant to
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

All test programs (and the preview/CLI tools **in fake mode**) point
`MOONDROP_CONFIG` at a scratch file, so they never read or write the user's
configuration.  The preview tool never connects on its own either: it disables the
backend's startup auto-connect, so rendering a page cannot take the live applet's
single control channel away.

## Safety nets worth knowing about

* **Model matching.** `DeviceProfile::namePatterns` are substrings, so a generic
  one is dangerous: a bare `EDGE` used to make "Samsung EDGE 5G" look like a
  MOONDROP EDGE and receive headphones-only commands.  Short names belong in
  `exactNames` (whole-name comparison).  `testProfiles()` in
  `tests/protocol.cpp` covers the foreign-name cases.
* **Offline gate.** `startChannelScan()` asks `BlueZWatcher` first: if Bluetooth
  reports the headphone as disconnected there is no point opening the control
  channel, so it waits for the watcher instead (and the UI says so).
* **Follow the headphone that is on.** Two mechanisms work together, and both are
  needed - removing either one brings back "I have to press Retry now":
  1. `startChannelScan()` checks `connectedMoondropAddress()` when the configured
     address is known to BlueZ but not connected, and follows another MOONDROP
     headphone that *is* connected.
  2. `BlueZWatcher::watchAllHeadsets()` reports *any* MOONDROP headphone that
     connects (`headphoneAppeared`), so `onHeadphoneAppeared()` can switch
     immediately.  The reconnect timer alone would be far too slow: the backoff
     reaches a minute, and `onDeviceVanished()` deliberately resets it to 2 s
     while nothing is connected.
  `moondrop-failover-check <offline-address>` covers (1) against real BlueZ state;
  `moondrop-reconnect-check` covers the retry-without-user-input path.
* **Qt cannot wildcard a D-Bus object path.** `QDBusConnection::connect()` matches
  the path exactly, so subscribing to `/` receives *nothing* from
  `/org/bluez/hci0/dev_XX` even though `dbus-monitor` shows the signal (verified
  against BlueZ 5.87).  `refreshAllHeadsets()` therefore enumerates device paths
  and subscribes to each.  Two further traps, both verified by experiment:
  passing an explicit signature string (`"sa{sv}as"`) makes the connection *fail
  silently*, and `sender()` is null in a system-bus slot - the emitting path is
  read from `QDBusContext::message().path()` instead.
* **Do not wrap a signal argument in `qdbus_cast<QVariant>()` again.** Qt
  demarshals `a{sv}` with the inner variant **already unwrapped**, so
  `changed["Connected"]` is a plain bool `QVariant`.  Wrapping it again gives an
  *invalid* QVariant whose `toBool()` is always `false` - and `isValid()` is
  false too, so nothing complains.  The effect was that every
  `Connected = true` signal was read as a *disconnect*: the watcher's
  `m_connected` stayed stuck at false, the backend was never told the headphone
  had come back, and the applet sat on "waiting for the headphone" until it was
  restarted.  This is the "it stopped connecting again" bug; it only shows up
  after a real disconnect/reconnect, never in a fake-transport test.
  `variantToBool()` in `devicediscovery.cpp` now unwraps only a `QDBusVariant`
  (the shape a `QDBusArgument` delivers) and returns every other value as-is;
  `moondrop-watchstate-check` feeds both shapes to the real slot without hardware.
* **The watcher parses the signal, not the state.** `onPropertiesChanged()` acts
  on the transition it is told about, so a missed or misread signal is the same
  thing as the headphone never coming back.  When touching that slot, remember
  both `deviceConnected` (the watched address) and `headphoneAppeared` (any
  MOONDROP device) are fired from it, and the second one is what the backend
  relies on to follow a pair the user switched to.
* **Device information is per connection.** Everything read from the headphone
  (model, firmware, serial, capability list, profile, battery, codecs) is cleared
  by `clearDeviceInfo()` when the link goes down and when another address is
  selected.  Without that the widget keeps describing the *previous* headphone
  after the user switches - and a stale capability list would silently disable
  features.  `moondrop-switch-check` covers the A -> disconnect -> B sequence.
* **EBUSY retry budget.** `m_busyRetries` is reset once per scan
  (`startChannelScan()`), never inside `probeNextChannel()` - resetting it there
  made the budget unreachable and the scan retried the same channel for ever.
  `moondrop-ebusy-check` pins this down: after the budget the error must be
  reported and `busy` must go false.
* **One control connection.** A MOONDROP headphone accepts a single RFCOMM data
  connection; every channel (1, 2, 16, ...) shares that one slot.  Consequences
  that are easy to get wrong: the channel scan must be **sequential** (parallel
  probes fight over the slot and the real channel can lose the race with EBUSY),
  and EBUSY means "the headphone is up but somebody holds the slot" - it never
  identifies the wrong channel.  `moondrop-scan-check` covers the "a wrong channel
  accepts the connection and then stays silent" case against the fake headset.
* **Socket per attempt.** Closing a socket whose `connect()` is still pending
  leaves the kernel's request alive for a moment, so the next `connect()` to the
  same device fails with EBUSY until it clears (~200 ms to ~1 s).  The scan
  therefore reuses one socket, shuts it down before closing, and waits before
  retrying the same channel.
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

## Packaging

Three channels exist, and they install the backend differently.  The difference is
not cosmetic; it is forced by how Qt resolves QML modules.

| Channel | Where the plugin lives | How the QML finds it |
| --- | --- | --- |
| Distro package (RPM/deb/PKGBUILD) | `/usr/lib*/qt6/qml/org/moondrop/backend` | `import org.moondrop.backend 1.0` (global module) |
| `.plasmoid` / KDE Store / `install.sh` | *inside* the package, `contents/ui/backend/` | `import "backend"` (directory import) |
| Source tree / build tree | `build/org/moondrop/backend` | global module via `QML_IMPORT_PATH=$PWD/build` |

`package/contents/ui/*.qml` always imports the **global** module, because that is
what the distro layout and the build tree provide.  `make-plasmoid.sh` rewrites
those imports to the directory form in its staging copy - so the store archive is
the only artifact that differs, and the source tree needs no duplication.

### Two traps when touching packaging

* **A module URI cannot be published from inside a package.**  A QML file that
  imports `org.moondrop.backend` has that URI resolved against Qt's *global*
  import path, which a user-installed package cannot write to.  A relative
  directory import resolves against the importing QML file instead, so it works
  from wherever the package was unpacked.  Verified against a real plasmashell
  (the plugin's `registerTypes()` runs from the package's own copy).
* **The bundled `qmldir` must not reuse the public module name.**  If the user has
  *both* the distro package and the store package, the globally installed module
  owns `org.moondrop.backend`, and the bundled copy is then refused with
  `Cannot install singleton type 'Moondrop' into protected module` - every page of
  the store package fails to load.  `make-plasmoid.sh` therefore writes the private
  `org.moondrop.backend.bundled`.  Leaving the `module` line out also works, but
  Qt logs a "does not contain a module identifier directive" warning on every
  start.  Both variants were measured against a plasmashell with the global module
  installed.

### The Qt version gate

`qtbase/src/corelib/plugin/qlibrary.cpp` refuses a plugin whose recorded Qt is
*newer* than the host:

```
plugin.minor > host.minor  ||  plugin.major != host.major   ->  "uses incompatible Qt library"
```

Consequences that are easy to get wrong:

* A store archive built on your newest development Qt **only runs on that Qt or
  newer**.  Build it on the oldest Qt you intend to support.  Measured on this
  project: Qt 6.8-built plugin loads on a Qt 6.11 plasmashell; the reverse is
  rejected with `uses incompatible Qt library. (6.11.0)`.
* `scripts/build-baseline-plugin.sh` builds the plugin in a container
  (`fedora:40`, Qt 6.8, the oldest available KF6/Qt6 pairing) and
  `MOONDROP_BACKEND_SO=... ./scripts/make-plasmoid.sh` picks that plugin up.
  Note that the project requires Qt 6.5, and Fedora 39 (Qt 6.5) has no KF6
  packages, so 6.8 is the practical floor for a KF6 build.
* The distro packages do not have this problem: they are rebuilt against the
  distribution's own Qt, so their plugin always matches the host.

### Scripts

| Script | Purpose |
| --- | --- |
| `scripts/make-plasmoid.sh [outdir]` | build the self-contained `.plasmoid` |
| `scripts/build-baseline-plugin.sh [image]` | build the plugin against an old Qt in a container |
| `scripts/install.sh` | the one line installer (distro package, else source) |
| `packaging/make-tarball.sh [outdir]` | source tarball for the distro packages (refuses a dirty tree) |
| `packaging/debian/make-deb.sh [outdir]` | build the `.deb` |
| `packaging/fedora/moondrop-control.spec` | `rpmbuild` / COPR |
| `packaging/arch/PKGBUILD` | `makepkg` / AUR |
| `packaging/appstream/*.metainfo.xml` | AppStream metadata (`appstreamcli validate` clean) |
| `scripts/store-check.sh` | end-to-end test of the store channel (see below) |
| `tools/ocs-provider.py` | local OCS provider used by that test |

The distro packages install the applet into `/usr/share/plasma/plasmoids/` rather
than running `kpackagetool6`: that is the shared location Plasma searches for
every user, so a system package does not have to be installed per account.
`kpackagetool6` remains the way a *user* installs a `.plasmoid`.

### Testing the store channel without publishing anything

`scripts/store-check.sh` runs the whole channel on the development machine:

1. builds the `.plasmoid` and installs it with `kpackagetool6`;
2. drives the **real KNewStuff client** (`KNSCore::EngineBase` +
   `Transaction::installLatest`, i.e. the code behind "Get New Widgets") against a
   local OCS provider, and asserts the archive came down and was unpacked;
3. loads the result in a real `plasmashell` (`plasmawindowed`) and asserts no page
   failed and that the pages resolved the bundled backend.

`tools/ocs-provider.py` is the provider it talks to.  Nothing leaves the machine
and the real store is never contacted; everything is written under a scratch XDG
root, so the user's own plasmoids are untouched.

Facts about the OCS protocol that the provider had to get right, each of which
cost a debugging round:

* **The client speaks XML, not JSON.**  `format=json` is only used when a caller
  asks for it; Attica's parser reads XML, and a JSON-only provider fails with
  `parseList():: XML Error: Start tag expected`.
* **The response shape is flat**: `{"status","statuscode","totalitems","data"}` -
  *not* the nested `{"ocs":{"meta":…,"data":…}}` form of OCS v2.  The old
  `/ocs/v1/content/data` endpoint on store.kde.org now answers `410 Gone`; the
  live service is `api.kde-look.org/ocs/v1/`.
* **A download link is resolved in two steps.**  The search result carries
  `downloadway`; link resolution asks
  `content/download/<contentid>/<linkid>` and expects
  `<downloadlink>`/`<mimetype>`/`<gpgfingerprint>` back under `details="download"`.
* **`ResultsStream` is lazy**: it does not talk to the provider until `fetch()`
  is called.
* **`knewstuff-dialog6` only accepts a knsrc file by name** from the standard
  search path (`$XDG_DATA_HOME/knsrcfiles/`), which is why the test needs a
  scratch XDG root rather than an argument.

The check needs the KNewStuff development files; where they are not installed,
point `MOONDROP_KNS_INCLUDE` at an unpacked copy.  Skipped steps are reported as
skips and the script exits non-zero, so a missing dependency cannot be mistaken
for a passing store test:

```bash
./scripts/store-check.sh                    # full run (needs kf6-knewstuff-devel)
```

## Repository layout note

The `ref-*/` directories are *other people's repositories*, cloned locally for
reading during development.  They are in `.gitignore` and must never be committed -
they carry their own licences and histories.  The same goes for `build/` and
`dist/`.

If you re-clone those sources to study a protocol, keep them outside the working
tree (or under `ref-`, which is ignored).
