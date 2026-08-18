# Strip QZ to a trainer bridge — specification

**Status: draft, under discussion.** Nothing here is committed to code yet. Numbers are
measured against the tree at the time of writing unless marked *estimate* or *verify*.

## 1. Goal

Reduce QZ to one job: carry data and control between the FTMS trainer and the four
training apps that matter — **Rouvy, Zwift, Kinomap, MyWhoosh** — with a UI that can be
operated mid-ride, and a settings surface a person can read.

Everything that is not that job is removed from the tree.

### Non-goals

- Supporting anyone else's hardware, or anyone else's workflow.
- Staying mergeable with upstream (see §3.1).
- Replacing what the training apps already do well: route selection, workout structure,
  activity recording, social features.
- **Making the BLE peripheral role work on Windows.** Explicitly deferred — see §3.2.1.

## 2. Decisions taken

Settled 2026-08-16:

| Question | Decision |
| --- | --- |
| Hosts | **Windows and Android now, Raspberry Pi later.** BLE virtual bike survives. |
| Strip depth | **Delete outright.** Accept permanent divergence from upstream. |
| UI | **New front-end on a kept bridge core.** homeform loses its UI role. |
| Recording | **Dropped entirely.** The training apps record; QZ does not. |

## 3. Hard constraints

These are properties of the platforms and the code, not preferences. The design has to
live with them.

### 3.1 Divergence is one-way

"Delete outright" means an upstream rebase stops being possible. Future upstream fixes
arrive only by reading a diff and reimplementing it by hand. This is a deliberate trade:
the tree becomes readable at the cost of the upstream safety net.

`FORK.md` §Versioning currently promises `v<upstream base>-qz.<n>` and says rebasing
"moves the base and resets the counter". **That sentence becomes false and must be
rewritten** when the first deletion phase lands.

### 3.2 The BLE peripheral role is not available on Windows

`virtualdevices/virtualbike.cpp` calls `QLowEnergyController::createPeripheral()`. Qt
supports the peripheral role on Linux/BlueZ, Android, iOS and macOS — **not Windows**.
That is why DIRCON exists in this fork and why it got so much attention.

| Host | Virtual bike over BLE | DIRCON (TCP/Wi-Fi) |
| --- | --- | --- |
| Windows | **No** — Qt has no peripheral role | Yes |
| Android | Yes | Yes |
| Raspberry Pi (future) | Yes, via BlueZ | Yes |

Consequence: **the Pi is the only planned host that gets both**, which makes it the
natural long-term home for the bridge rather than a curiosity.

Note the two BLE roles are independent, and only one is affected. The **central** role —
QZ connecting *to* the trainer — works on Windows and is where this fork's WinRT work
went. Nothing in this spec touches it.

### 3.2.1 The peripheral code is kept, and stays inert on Windows

Decided 2026-08-16. `virtualdevices/*.cpp` are in the unconditional `SOURCES` list and
`createPeripheral()` (`virtualbike.cpp:447`) has no platform guard, so the peripheral
stack already compiles into the Windows build. **It stays.** Android and the future Pi
need those exact files, so this costs nothing — it follows from §3.4 rather than being a
separate concession.

What it is *not* is a working Windows fallback. Under Qt the controller never advertises.
The code is present, compiled and inert.

Making it genuinely work is **out of scope for the strip**: it would mean publishing an
FTMS GATT server through WinRT's `GattServiceProvider`, which Windows does support
natively and Qt simply does not wrap. That is new construction of roughly the scale of the
WinRT central-role work, not preservation of something existing, and it does not belong in
a deletion project.

Recorded as a candidate follow-on, gated on the transport tests in §12. If Rouvy, Zwift
and Kinomap all reach QZ over DIRCON from Windows, it is a failsafe for a path that never
fails and should never be built.

### 3.2.2 Kinomap is Android-only, and best-effort

Established 2026-08-16: Kinomap has no Windows client at all, so it places **no
requirement on the Windows build** and does not affect §12's Windows question.

The intended topology is Kinomap and QZ on the same Android device. That is unlikely to
work: Android's Bluetooth adapter does not receive its own advertisements, so a BLE
central on the device cannot discover or connect to a GATT server published by that same
adapter. Same-device bridging would require Kinomap to accept DIRCON over localhost, and
Kinomap is a BLE/ANT+ trainer client.

**Declared best-effort by the rider** — if it does not work, it is not a defect and not
worth engineering time. Realistically it needs a second device or the future Pi. No phase
in §10 is gated on it and no design decision should be bent toward it.

### 3.2.3 Confirmed topology

Measured, not assumed. All four apps accounted for; MyWhoosh's row re-measured
2026-08-17 and it no longer says what it used to:

| App | Host | Reaches QZ via | Status |
| --- | --- | --- | --- |
| Rouvy | Windows | DIRCON | **Confirmed working** |
| Zwift | Windows | DIRCON | **Confirmed working** — discovered as "ELITE AVANTI" |
| MyWhoosh | Windows | DIRCON | **Confirmed working** 2026-08-17, same host as QZ (§3.3) |
| Kinomap | Android | BLE | Best-effort, likely needs a 2nd device (§3.2.2) |

The conclusion that matters for scope: **no app that runs on Windows needs the BLE
peripheral role.** Windows is fully served by DIRCON, which this fork has already
hardened. Windows keeps its place, and §3.2.1 stays out of scope with no remaining
argument against it.

### 3.3 MyWhoosh does share a machine with QZ — the earlier constraint was wrong

**Superseded 2026-08-17.** This section used to state, as settled fact, that MyWhoosh
refuses a DIRCON device advertising the machine's own IP, citing upstream
[issue #3314](https://github.com/cagnulein/qdomyos-zwift/issues/3314), and concluded that
MyWhoosh could only ever be served by QZ on Android or the Pi. That is not what the code
does, and the conclusion was load-bearing for scope, so the correction is recorded rather
than quietly applied.

There is no IP check. There were four independent bugs on QZ's side, each completely
hidden by the previous one, which is why it read as a single hard rule for so long:

| Fix | Bug | Symptom |
| --- | --- | --- |
| `daa73a305` | SRV hostname must equal the instance name with spaces hyphenated | never discovered |
| `6849b694f` | AAAA advertised while the listener binds `AnyIPv4` | discovered, stuck in `SYN_SENT` |
| `343e42b90` | `0x2ACC` feature bitmask lacked bit 14, power measurement | connected, `0x2AD2` never read |
| `f8af0cb21` | unsolicited zero-filled `0x2AD2` frame sent on connect | subscribed, then disabled, 0 W |

The last is the one worth remembering: a client may latch which quantities the trainer
reports from the flags of the *first* Indoor Bike Data frame it sees. All-zero flags mean
"measures nothing". Nothing may push a hand-built frame there again.

Confirmed with data flowing, on one Windows box, with Rouvy connected at the same time.
MyWhoosh additionally needs Apple's Bonjour service running on Windows, which is an
install-time fact about MyWhoosh and not a constraint on this fork.

**Consequences for this spec.** No host is kept alive for MyWhoosh's sake. Windows serves
all three of the apps that run on it (§3.2.3), Android is kept for Kinomap and as the
second host (§3.2.2), and the Pi remains a *future* target justified by BLE peripheral
support — never by MyWhoosh.

### 3.4 Linux/BlueZ code is dormant, not dead

The Pi is a future target, so the strip **must not** delete Linux-guarded code as
"unused platform". Rule for every deletion: remove code by *feature*, never by
*platform*. `#if defined(Q_OS_LINUX)` blocks inside a kept feature stay.

CI still carries `raspberry-pi-build`, `raspberry-pi-build-and-image-64bit` and
`raspberry-pi-smoke-test` jobs. They stay in the workflow file even while disabled.

### 3.5 The new UI must be Qt 5 source-compatible

Android is on Qt 5; the Pi will likely be too. Windows is Qt 6.8.2. The existing scheme —
QML written for Qt 5, Qt 6 variants generated at build time — is what makes one tree serve
both, and the new front-end must obey it. **No Qt 6-only QML.** This rules out some of the
more attractive modern QML idioms and should be checked before the UI design is fixed,
not after.

### 3.6 Settings bookkeeping is mechanical and enforced

Per `src/CLAUDE.md`: new settings go **last** in `settings.qml` (and last in any other QML
declaring them), `allSettingsCount` in `qzsettings.cpp` must match, and
`settings-catalog.json`'s `settingCount` must match its array length.

Mass *deletion* is not covered by those rules and is the riskier direction — a removed
setting that some QML still binds to fails at runtime, not at build time. Every settings
deletion phase needs a QML-binding sweep, not just a recount.

### 3.7 Licence

GPLv3, unchanged. Attribution to Roberto Viola stays regardless of how much is deleted.

## 4. Measured starting state

| Surface | Now |
| --- | --- |
| `settings.qml` | 15,554 lines, 88 collapsible sections, ~45 of them machine-specific |
| `homeform.cpp` | 10,688 lines |
| Settings keys (`allSettingsCount`) | 1,010 |
| …of which tile plumbing | **471** (91 tiles × visible/order + defaults) |
| Device drivers | 18 (already cut from upstream's 132) |
| QML files in `src/` | 50 |

### 4.1 Coupling, measured

The good news, and it is better than it looked:

- `virtualbike.cpp` includes only `virtualbike.h`, `qtbluetoothcompat.h`, `devices/bike.h`.
  **The virtual-device layer has no dependency on the UI at all.**
- `trainprogram` appears **zero times** anywhere under `devices/`. Training programs are
  purely a homeform-layer concern and do not touch the bridge path.
- `homeform` appears in `devices/` only 27 times: 23 in `bluetooth.cpp`, 3 in `bike.cpp`,
  1 in `bluetooth.h`.

So the bridge core is very nearly free-standing already. The entanglement is concentrated
in `homeform.cpp`, which is exactly the file being retired.

The bad news:

- `homeform.h` holds `trainProgram`, `previewTrainProgram`, `strava`, `garminConnect`,
  `intervalsicu`, `pelotonHandler` as direct members, with ~373 `peloton` and ~365
  `trainprogram` references through the file. These come out together or not at all.
- `ftmsbike.cpp` includes `devices/cscbike/cscbike.h`. **`cscbike` cannot be deleted
  without first breaking that dependency** — verify what it is used for before assuming
  the driver cull is free.

## 5. Target architecture

Three layers, with the dependency arrows pointing one way only.

```
  ┌─────────────────────────────────────────────┐
  │  UI (new, small, Qt5-compatible QML)         │
  │  connection state · gears · resistance ·     │
  │  ERG toggle · DIRCON/BLE status · settings   │
  └──────────────────┬──────────────────────────┘
                     │ properties + signals, no logic
  ┌──────────────────▼──────────────────────────┐
  │  Bridge core (kept, largely unchanged)       │
  │  bluetooth.cpp · devices/ · virtualdevices/  │
  │  dircon · qmdnsengine · gears · gamepad      │
  └─────────────────────────────────────────────┘
```

The bridge core is the asset. It is the part that took the measurements in
[docs/fork/](README.md) to get right, and it should be touched as little as possible
during the strip. **If a phase requires editing the BLE or DIRCON path to remove a
feature, that is a signal the phase is wrong.**

`homeform` is not replaced by one class. Its two jobs separate: the ~27 bridge-facing
references become a small session/state object; the UI role goes to the new front-end.

## 6. Keep list

Proposed. Anything not listed is a deletion candidate in §7.

**Bridge:** `devices/bluetooth.*`, `devices/bike.*`, `devices/bluetoothdevice.*`,
`devices/ftmsbike/`, `devices/heartratebelt/`, `devices/dircon/`,
`virtualdevices/` (**all platforms including Windows** — §3.2.1),
`qmdnsengine/`, `qtbluetoothcompat`, `windowsblebond`, `definitions`, `metric`,
`inclinationresistancetable`, `wheelcircumference`, `localipaddress`.

**Platform/infra:** `main`, `logwriter`, `signalhandler`, `keepawakehelper`,
`qzforkversion`, `qzsettings`, the Android glue (`androidactivityresultreceiver`,
`androidqlog`, `androidadblog`, `androidstatusbar`).

**Fork features:** `gamepadcontroller`, `rtssosd`, `customgears.qml`, `gears.qml`.

**Verify before assuming kept:** `ergtable`, `treadmillErgTable`, `sessionline`,
`simplecrypt`, `handleurl`, `filedownloader`, `mywhooshlink`, `cscbike` (see §4.1).

## 7. Deletion inventory

Grouped by how independent each group is. Order matters: the leaves come out first, so
that each phase ends with a tree that still builds and still rides.

### Group A — leaf integrations (independent, low risk)

Peloton, Strava, Garmin Connect, Intervals.icu, HomeFitnessBuddy, PowerZonePack.

`peloton.*`, `garminconnect.*`, `homefitnessbuddy.*`, `powerzonepack.*`, the OAuth glue,
`WebPelotonAuth.qml`, `WebStravaAuth.qml`, `WebIntervalsICUAuth.qml`, plus their homeform
members and settings (~55 keys).

### Group B — recording, history and charts

Follows from "recording: dropped entirely".

`qfit.*`, `fit-sdk/` (314 files), `fitbackupwriter`, `fitdatabaseprocessor`,
`charts`/`ui_charts`, `ChartsEndWorkout*.qml`, `ChartFooter*.qml`, `PreviewChart.qml`,
`WorkoutsHistory.qml`, `workoutmodel`, `workoutloaderworker`, the e-mail report and
`smtpclient/` (74 files).

### Group C — training programs and media

`trainprogram.*`, `zwiftworkout.*`, `zwo/`, `gpx.*`, `gpx/`, `kmlworkout`,
`WorkoutEditor.qml`, `TrainingProgramsList*.qml`, `GPXList.qml`, `videoPlayback.qml`,
`GoogleMap.qml`, `PathController`, and the Maps/Video settings sections.

Confirmed independent of the bridge (§4.1), so this is a homeform-only excision despite
its size.

### Group D — telemetry, templates and remote control

`templateinfosender*`, `tcpclientinfosender`, `webserverinfosender`, `mqtt/` + 
`mqttpublisher`, `osc`/`oscpp/`, `QTelnet`, `TemplateTcpClient.qml`,
`TemplateWebServer.qml`, `inner_templates/` (59 files), `templates/`.

*Verify first:* the template system is how some overlays get data. Confirm the RTSS path
does not route through it before deleting.

### Group E — remaining device drivers

Down to `ftmsbike` + `heartratebelt` + `dircon`, per the long-standing goal of supporting
only the FTMS trainer. Removes `smartspin2k`, `stagesbike`, `strydrunpowersensor`,
`coresensor`, `moxy5sensor`, the four Elite accessories, `fitmetria_fanfit`,
`wahookickrheadwind`, `sramAXSController`, `cycplusbc2controller`, `thinkridercontroller`.

*Blocked on:* the `cscbike` dependency in `ftmsbike.cpp` (§4.1).

### Group F — the UI itself

The tile system (91 tiles, 471 settings), `settings.qml`'s 88 sections,
`settings-tiles.qml`, `settings-shortcuts.qml`, `settings-tts.qml`,
`settings-treadmill-inclination-override.qml`, `Wizard.qml`, `profiles.qml`,
`Classifica.qml`, `SwagBag*.qml`, and finally `homeform.cpp` itself.

This is last because everything else must be gone before the replacement UI's scope is
even knowable.

### Group G — other machine types

`treadmill.*`, `rower.*`, `elliptical.*`, `stairclimber.*`, `jumprope.*` and their
virtual counterparts, once no kept driver refers to them.

*Verify:* `bluetooth.cpp` may switch on these types generically.

## 8. Settings plan

Target: **under 150 keys**, from 1,010. *Estimate*, not a measurement — it falls out of
the deletions above rather than being designed independently.

| Source of deletion | Keys *(approx)* |
| --- | --- |
| Tile system | 471 |
| Machine-specific device panels | ~200 |
| Shortcuts / TTS | ~108 |
| Peloton / Garmin / Strava / training programs | ~75 |
| Remainder: bike, gears, gamepad, virtual device, DIRCON, HR, UI | target ~150 |

The surviving settings should be reorganised around the four things a rider changes —
**bike, gears, training-app connection, display** — not around vendors.

## 9. UI specification

Decisions taken 2026-08-16: **gear-centric ride screen**, **the same UI on every
platform**, **one settings page in four groups**, **built as a parallel QML tree behind a
flag**.

### 9.1 What the old UI was, measured

Two numbers set the scope. The navigation drawer has 15 entries:

`Settings · Workouts History · Swag Bag · Charts · Open GPX · Open Train Program ·
Workout Editor · What's On Zwift · Save GPX · Save FIT · Wizard · Help · Community ·
Credits · Quit`

Everything from *Workouts History* through *Save FIT* — 9 of 15 — is deleted by §7. What
remains is **Settings, Help/Credits, Quit**, which does not justify a drawer.

`homeform.h` exposes 91 `Q_PROPERTY` and 55 `Q_INVOKABLE`. Only about a dozen are bridge
data (`device`, `bluetoothDevices`, `currentSpeed`, `metrics`, `autoResistance`, `signal`,
`info`, `lap`, `start`/`stopRequested`). The rest are tile-rendering plumbing on
`DataObject`, integration login state, and workout/chart point arrays — all deleted.

A third win is free: with one driver kept, the **"Select Your Gym Device" picker becomes
pointless**. QZ connects to the only trainer it supports. The screen and its 10-second
refresh loop go away.

### 9.2 The QML/C++ contract

QML must not talk to `homeform`. A single new object carries the ride:

`src/ui/ridestate.{h,cpp}` — roughly 12 properties and 4 invokables, wrapping the bridge
core and nothing else:

| Kind | Members |
| --- | --- |
| Connection | `trainerConnected`, `trainerName`, `appConnected`, `appName`, `transport` (BLE/DIRCON) |
| Ride | `gear`, `resistance`, `power`, `cadence`, `speed`, `heartRate`, `ergMode` |
| Actions | `gearUp()`, `gearDown()`, `setGear(int)`, `toggleErg()` |

Keeping this surface small is the point of the exercise. **If it grows past ~20 members,
something UI-shaped has leaked back into the bridge**, and that is the signal to stop and
reconsider.

### 9.3 Navigation

No drawer. Three destinations in a `TabBar`, **Ride first and default**:
`Ride · Setup · Settings`.

### 9.4 Screen 1 — Ride (gear-centric)

The gear dominates, because shifting is the only thing the rider does *through QZ*
mid-ride. Everything else is confirmation.

```
┌────────────────────────────────────────┐
│ ● Trainer      ● Zwift (DIRCON)        │
├────────────────────────────────────────┤
│                                        │
│              ┌─────────┐               │
│    [  −  ]   │    7    │   [  +  ]     │
│              └─────────┘               │
│                 res 14                 │
│                                        │
│   184 W · 87 rpm · 31.2 km/h · 142 ♥   │
│                                        │
│              ERG  [ OFF ]              │
└────────────────────────────────────────┘
```

- Gear numeral is the largest element on screen, readable from the bars at arm's length.
- `res 14` beneath it is the **absolute** resistance the gear resolves to, matching the
  neutral-gear table semantics already built into `bike.cpp`.
- `−`/`+` are sized for a thumb on a tablet, not a mouse pointer.
- Metrics are one secondary line. They are confirmation, not a dashboard.
- **ERG must not be reachable by accident.** It changes how the bike behaves mid-effort;
  require a deliberate press, and consider a hold.
- Status pills name the *transport*, because "connected" over BLE and over DIRCON fail in
  different ways and the distinction is the first thing worth knowing when debugging.

### 9.5 Screen 2 — Setup

Trainer connection state, virtual-device mode, discovery status, and the gear table.

From §3.2.1: on Windows the setup screen **must not present the BLE virtual device as a
working option**. The code is compiled in but cannot advertise. Show it as unavailable on
this platform, with the reason stated — an enabled-looking toggle that silently does
nothing is exactly the UX failure this project exists to remove.

### 9.6 Screen 3 — Settings

One scrollable page, four groups, organised around what a rider changes rather than
around vendors:

**Bike** · **Gears** · **Training-app connection** · **Display**

No nesting and no accordions. If a group cannot fit legibly on one page, that is evidence
§8 did not delete enough, not a reason to add hierarchy.

### 9.7 Platform behaviour

One QML tree, identical on both platforms — no per-platform layouts. Windows is a
resizable window; Android is touch. Sizing must therefore be relative, with touch targets
dimensioned for the tablet case, which is the stricter of the two.

The Windows ride screen is accepted as rarely-viewed: RTSS already overlays gear, ERG and
resistance on the fullscreen training app. It is built anyway because a second layout
costs more to maintain than an unused screen costs to render.

### 9.8 Build mechanics

Verified against `qdomyos-zwift.pri` and `tools/qt6-qml-imports.py`:

- New QML lives in `src/ui/` and **must be listed in `src/qml.qrc`**. The Qt 6 variants
  are then generated at build time into `$$OUT_PWD/qml6` with no further action.
- The generator rewrites **imports only** — it drops versions for modules with no Qt 6
  equivalent and maps `QtGraphicalEffects` → `Qt5Compat.GraphicalEffects`. `QtQuick`,
  `QtQuick.Controls`, `QtQuick.Layouts` and `QtQuick.Window` are untouched because Qt 6
  still accepts their 2.x imports.
- It does **not** translate APIs. A Qt 6-only type or property will build on Windows and
  fail on Android. This is the single easiest way to break the Android build, and it will
  not be caught by the Windows compile (§3.5).
- The resource alias must stay verbatim; the rewritten copy lives elsewhere on disk but
  must still resolve as `qrc:/…`.

### 9.9 Migration

Parallel tree, flag-switched:

1. Add a `ui_next` boolean setting, default **false** (§3.6 bookkeeping applies: last in
   `settings.qml`, `allSettingsCount`, `settings-catalog.json`).
2. `main.cpp` chooses the entry QML on that flag. Both trees ship for one release.
3. Flip the default once the new UI has ridden with Rouvy and Zwift.
4. Delete the old tree and the flag together, as Group F.

The old UI stays working throughout, so a bad ride costs a settings toggle rather than a
rebuild. This is why the ride screen is not built last: it needs real rides to validate,
and those take calendar time rather than effort.

## 10. Phasing

Each phase must end with a tree that **builds on both Windows and Android** and **passes
the suite, including the Layer C ride loop** ([VIRTUAL-BIKE.md](VIRTUAL-BIKE.md)). A ride
with Rouvy is still the final word, but it is no longer the only evidence available — see
§11.1, and the coverage column below.

| Phase | Content | Risk | Harness |
| --- | --- | --- | --- |
| 0 | Rewrite `FORK.md` §Versioning (§3.1); tag a pre-strip release as the fallback | none | n/a |
| 1 | Group A — leaf integrations | low | **covered** |
| 2 | Group B — recording | low | **covered** |
| 3 | Group C — training programs | medium (homeform surgery) | **none** |
| 4 | Group D — telemetry | medium (verify RTSS first) | **covered** |
| 5 | Group E — drivers | low, once cscbike is resolved | **covered** |
| 6 | Settings consolidation | medium (§3.6 runtime failures) | **covered** |
| 7a | `RideState` object + `ui_next` flag + new tree under `src/ui/` | medium | **none** |
| 7b | Ride on the new UI with Rouvy and Zwift; flip the default | low, but needs calendar time | **none** |
| 7c | Delete Group F — old tree, tile system, `homeform.cpp`, the flag | high | **none** |
| 8 | Group G, Pi build revival | medium | partial |

"Covered" means the end-to-end loop asserts on that phase's blast radius: bike frames in,
metrics, DIRCON, a client reading numbers back out. It is deliberately **not** UI coverage
— `homeform` cannot be constructed without a QML engine, so the loop asserts on the wire
rather than on tiles. That is the whole of the distinction in the column.

Which argues for taking the covered phases first. The numbering here is from the original
plan and the §11.6 criteria refer to it, so it stays; but **1, 2, 5, 4, 6 before 3 and 7**
spends the safety net where it exists and arrives at the UI work — the part that has to be
validated by riding — against a much smaller tree.

Phase 0 matters more than it looks: once phase 1 lands, there is no going back to
upstream. A tagged release beforehand is the only rollback.

## 11. Verification

Constraint set 2026-08-16: **most work happens in a remote browser session** with no local
build and no trainer. Hardware tests are therefore a scarce resource and everything that
can run in GitHub Actions must.

### 11.1 The harness that already exists

An earlier draft of this spec claimed there was "no unit-test safety net". That was wrong.

`tst/qdomyos-zwift-tests.pro` is a **gtest + gmock suite** built by the top-level
`qdomyos-zwift.pro` on every non-iOS, non-Android target, linked against
`src/qdomyos-zwift-lib.pro`. Because it links the library rather than the app, **it can
exercise the entire bridge core without a UI and without hardware.**

It runs in CI today. `linux-x86-build` was re-enabled as a tests-only job — it publishes no
binary, sits outside every `needs:` chain, and runs
`GTEST_OUTPUT=xml:test-results/ ./qdomyos-zwift-tests` at
[main.yml:776](../../.github/workflows/main.yml#L776), uploading result XML on failure.

Existing suites, and their fate:

| Suite | Covers | Fate |
| --- | --- | --- |
| `TestFtmsControlPointHandshake` | FTMS 0x2AD9 control point | **Keep** — core |
| `TestResistanceSlewLimiter` | resistance rate limiting | **Keep** — core |
| `TestServiceSubscriptionPlan` | GATT service subscription | **Keep** — core |
| `bluetoothdevicetestsuite` + `devicetestdataindex` | data-driven device discovery | **Prune** to kept drivers (Phase 5) |
| `ergtabletestsuite`, `TestErgTableSelection`, `TestErgAutoMode` | ERG tables | Keep unless `ergtable` is cut |
| `TestZwiftRideController` | Zwift Play/Click | Follows open question 6 |
| `garminconnecttestsuite` | Garmin | **Delete** with Phase 1 |
| `qfittestsuite`, `testtrainingloadtestsuite` | FIT writing, training load | **Delete** with Phase 2 |
| `trainprogramtestsuite`, `zwiftworkouttestsuite` | training programs, ZWO | **Delete** with Phase 3 |
| `testsettingstestsuite` | settings plumbing | **Keep and extend** (§11.5) |

**Deleting a feature without deleting its suite breaks the build, not the tests.** Each
phase below therefore names the suites that go with it.

Since this section was written the virtual-bike work ([VIRTUAL-BIKE.md](VIRTUAL-BIKE.md))
added the end-to-end half it was missing. These are the suites the "covered" column in §10
refers to, and none of them is a candidate for deletion:

| Suite | Covers | Layer |
| --- | --- | --- |
| `TestRideScenario`, `TestSimulatedBikeAnnouncement` | scripted rides, `.ride` fixtures, the simulated bike | A |
| `TestFtmsFrameHarness` | the shipped `ftmsbike` parser, byte-exact against recorded frames | B |
| `TestDirconFakeApp` | a training app connecting and enumerating over DIRCON | C |
| `TestDirconRideLoop` | bike → metrics → DIRCON → client, asserted over a whole ride | C |
| `TestDirconDiscovery` | mDNS advertisement, the Rouvy and MyWhoosh profiles | C |

The loop runs entirely in-process over loopback TCP: no radio, no trainer, no training app.
What it does not touch is the UI — `homeform` needs a QML engine — so it proves the bridge
survived a deletion, and says nothing about whether the screen did.

### 11.2 The browser-session workflow

`on:` carries `pull_request:` with **no branch filter**, while `push:` is restricted to
`main`. So:

- Pushing a `strip/phase-N` branch alone triggers **nothing**.
- Opening a **PR** from it runs the full matrix: `window-build`, `window-qt6-build`,
  `android-build`, and `linux-x86-build` (the tests).

The working pattern for every phase is therefore **branch → PR → green → merge**. No phase
is merged on a red or un-run PR. This is the only practical review mechanism when the
author cannot build locally.

Note `window-msvc2022-build` and the two peloton-bike jobs are gated on
`github.event_name == 'schedule'` and the cron is dropped, so they are dormant rather than
enabled — do not wait on them.

### 11.3 What CI can and cannot prove

| CI can prove | Only hardware can prove |
| --- | --- |
| It compiles on Windows (Qt6/MSVC) and Android (Qt5) | The trainer actually connects |
| The gtest suite passes | Resistance *feels* right |
| Gear maths, resistance mapping, FTMS framing | The training app discovers QZ on a real LAN |
| Settings integrity and QML lint | Shifting is responsive under load |
| DIRCON serves a loopback client (§11.5) | Nothing regressed subjectively |

The design goal of §11.5 is to move as much as possible from the right column to the left.

### 11.4 Universal gates — every phase, no exceptions

A phase passes only when **all** hold:

1. **PR is green** on all four active jobs.
2. **gtest suite passes** with no test deleted except those the phase explicitly names.
3. **No new compiler warnings** introduced by the phase's own files.
4. **`git grep` finds no dangling references** to any deleted symbol, file, QML component,
   or setting key.
5. **Settings integrity holds**: `allSettingsCount` matches, `settings-catalog.json`'s
   `settingCount` matches its array, and every setting name bound in QML exists in
   `qzsettings.h` (§11.5, check 3 — this is the one that fails at *runtime* otherwise).
6. **The app still starts headless**: `qdomyos-zwift -no-gui` exits 0 on the Linux runner.

Gate 4 matters more than it looks. QML binds settings by name with no compile-time check,
so a deleted key surfaces as a blank or broken control on the bike, days later.

### 11.5 New test infrastructure to build

Ordered by value. Items 1–3 should land **before Phase 1**, because they are the
regression net every later phase leans on.

**1. Gear-table tests** *(gtest, no hardware)* — lock in the work already done and
validated on the bike: a 15-row table with neutral gear 7 maps gears 1–15 to resistances
7,8,9,10,11,13,14,16,17,19,21,23,26,29,32; `gearsModifier()` is 0 at neutral;
`gearsIndexOffset()` is centred so neutral adds no slope; gear changes clamp at both ends.
This is the highest-value test in the project — it is behaviour that was expensive to get
right and is invisible until you are on the bike.

**2. Grade→resistance test** *(gtest)* — pin the formula
`grade% × 1.5 + bike_resistance_offset + 1 + CRR + CW`, so the flat-terrain calibration
(offset 13) cannot silently regress.

**3. Settings-integrity check** *(script, runs in CI)* — three assertions: `allSettingsCount`
equals the declared count; catalog `settingCount` equals its array length; and **every
setting name referenced in any `.qml` resolves to a key in `qzsettings.h`**. The third is
new and is what makes mass deletion safe.

**4. QML lint on both dialects** *(CI step)* — `qmllint` over the Qt 5 sources *and* the
generated Qt 6 variants in `$$OUT_PWD/qml6`. This is the gate for §9.8's trap, where a Qt
6-only API compiles on Windows and breaks Android. Without it, that class of bug reaches
the tablet.

**5. DIRCON loopback test** *(gtest)* — start the DIRCON server, connect a client over
loopback, assert the FTMS service and characteristics are served and that a resistance
write round-trips. **This is the single biggest hardware-test saving in the plan**: it
converts "ride with Rouvy to check the bridge still works" into a CI check.

**6. mDNS advert test** *(gtest)* — assert the advertised service type is fully qualified,
the SRV target is a legal hostname with no `\032` escaping, announcements go to all
interfaces, and a goodbye is sent on quit. Every one of these is a bug this fork already
fixed once; none is currently protected against regression.

**7. Headless smoke test** *(CI step)* — `-no-gui` startup on the Linux runner: clean
start, DIRCON listening, clean exit. Cheap, and catches whole classes of deletion damage.

**8. `RideState` contract test** *(gtest, Phase 7a)* — every property and invokable in §9.2
exists and updates. Also asserts the surface has **not grown past 20 members**, mechanising
the §9.2 tripwire.

### 11.6 Per-phase passing criteria

Universal gates (§11.4) apply throughout and are not repeated.

**Phase 0 — versioning correction**
*Criteria:* `FORK.md` no longer claims upstream rebasing is supported; a pre-strip release
tag exists. *Test:* documentation only; CI green.
*Status:* **done 2026-08-18.** `FORK.md` §Versioning and the header comment in
`src/qzforkversion.h` now say the base does not move, and `v2.21.6-qz.2` is tagged at the
tip of `lite-version` with `QZ_FORK_VERSION` bumped to match.

An earlier draft called the tag half satisfied by `v2.21.6-qz.1`. That stopped being true:
`qz.1` predates the whole virtual-bike test stage, so rolling back to it would have
discarded the safety net along with the strip. The fallback has to be the tree as it stands
on the eve of the first deletion, which is what `qz.2` is.

**Phase 1 — leaf integrations**
*Criteria:* no Peloton/Strava/Garmin/Intervals.icu/HomeFitnessBuddy/PowerZonePack symbol,
file, QML page, OAuth handler, or setting remains. `garminconnecttestsuite` deleted with
them. ~55 setting keys removed and all three counts reconciled.
*Tests:* gtest green minus the named suite; settings-integrity check; headless smoke.
*Hardware:* **none.** Nothing in this group touches the bridge.

**Phase 2 — recording**
*Criteria:* FIT writing, history, charts, e-mail report and `smtpclient/` gone.
`qfittestsuite` and `testtrainingloadtestsuite` deleted; `tst/test-artifacts/*.fit` and
`*.sqlite` removed.
*Tests:* as Phase 1. Confirm the app starts and rides with no session storage — the
absence of a writer must not fault the metric pipeline.
*Hardware:* **none.**

**Phase 3 — training programs**
*Criteria:* `trainprogram`, ZWO, GPX, KML, video and maps gone; `trainprogramtestsuite` and
`zwiftworkouttestsuite` deleted; `homeform.h` no longer declares `trainProgram` or
`previewTrainProgram`.
*Tests:* as above, **plus the DIRCON loopback test (§11.5 item 5)** — this is the first
phase large enough to plausibly disturb the control path, and that test is what makes
skipping a hardware ride defensible.
*Hardware:* **none, if item 5 exists.** If it does not, this phase needs a ride — which is
the argument for building it first.

**Phase 4 — telemetry and templates**
*Criteria:* templates, MQTT, OSC, telnet, webserver info senders gone.
*Precondition:* verified that the RTSS overlay does **not** route through the template
system. If it does, the overlay is re-pointed before anything is deleted.
*Tests:* as above. RTSS overlay still displays gear/ERG/resistance — observable on the
Windows desktop without the bike, by running QZ and watching the overlay.
*Hardware:* **none.**

**Phase 5 — driver cull**
*Criteria:* only `ftmsbike`, `heartratebelt`, `dircon` remain; the `cscbike` dependency in
`ftmsbike.cpp` resolved (open question 5); `bluetoothdevicetestsuite` data pruned to the
kept drivers and still green.
*Tests:* device-discovery suite green against the reduced set; FTMS handshake and slew
limiter suites unchanged and green.
*Hardware:* **yes — H1.** This is the first phase that edits the device layer. One session:
trainer connects, gears shift 1–15, resistance tracks the table.

**Phase 6 — settings consolidation**
*Criteria:* key count at or under target; the four groups of §9.6 hold; no QML binds a
missing key.
*Tests:* settings-integrity check is the primary gate here. Gear-table and
grade→resistance tests (items 1–2) must still pass — they are what proves the calibration
survived the reshuffle.
*Hardware:* **yes — H2.** Short session confirming gear 7 on the flat still lands near
resistance 14. Settings changes are exactly the kind that pass every automated check and
still ride wrong.

**Phase 7a — new UI behind the flag**
*Criteria:* `ui_next` defaults false; the old UI is untouched and still default; new tree
builds on Windows *and* Android; `RideState` contract test passes.
*Tests:* QML lint on both dialects (item 4) is the gate. Contract test (item 8).
*Hardware:* **none** — the flag is off.

**Phase 7b — flip the default**
*Criteria:* the new UI has ridden successfully with both Rouvy and Zwift.
*Tests:* no automated substitute exists. This is an acceptance test by definition.
*Hardware:* **yes — H3 and H4.** One ride per app. Gear display and shifting correct,
connection status accurate, ERG toggle behaves, nothing unreadable from the bars.

**Phase 7c — delete the old UI**
*Criteria:* `homeform.cpp`, the tile system, `settings.qml`'s 88 sections and the
`ui_next` flag all gone; no QML references a deleted component.
*Tests:* full suite; QML lint; headless smoke. Gate 4 does the heavy lifting.
*Hardware:* **none** — 7b already validated the replacement, and this phase only removes
the thing it replaced.

**Phase 8 — other machine types, Pi revival**
*Criteria:* treadmill/rower/elliptical/stairclimber/jumprope gone with no kept driver
referring to them; the Pi jobs re-enabled and green.
*Tests:* full suite; the Pi smoke-test job.
*Hardware:* Pi only, and only when that target is actually pursued.

### 11.7 Hardware budget

Four sessions for the whole project:

| | After | Purpose |
| --- | --- | --- |
| **H1** | Phase 5 | Trainer connects; gears 1–15 shift; resistance tracks the table |
| **H2** | Phase 6 | Calibration survived the settings reshuffle |
| **H3** | Phase 7b | New UI, ride with Rouvy |
| **H4** | Phase 7b | New UI, ride with Zwift |

Phases 1, 2, 3, 4, 7a and 7c need **no hardware at all**, provided §11.5 items 1–5 exist.
That is the return on building the test infrastructure first, and the reason items 1–3 are
scheduled before Phase 1 rather than alongside it.

Separately pending, and unrelated to the strip: the startup-gear fix and the
`bike_resistance_offset` 18→13 calibration both still want a confirming ride.

## 12. Open questions

1. ~~**Kinomap's transport.**~~ **Resolved 2026-08-16:** Kinomap has no Windows client,
   so it imposes nothing on the Windows build. Reduced to a best-effort Android case —
   see §3.2.2.
2. ~~**Zwift's transport from Windows.**~~ **Resolved 2026-08-16:** Zwift connected to
   QZ over DIRCON from Windows, discovering it as "ELITE AVANTI". See §3.2.3.
3. ~~**Does the Windows build survive?**~~ **Resolved 2026-08-16: yes**, and more
   strongly since. Windows serves all three apps that run on it — Rouvy, Zwift and, as of
   2026-08-17, MyWhoosh (§3.3) — over DIRCON. It stays a first-class host, the §10 phasing
   is unaffected, and the deferred WinRT peripheral work (§3.2.1) stays buried.
4. **Heart rate:** is the HR belt used, or does the training app read it directly?
5. **`cscbike`** — what does `ftmsbike.cpp` actually use it for?
6. **Zwift Play / Click** controllers (`zwift_play/`, `zwift-api/`) — kept or cut? They
   are a shifting input, which overlaps with the gamepad work.
