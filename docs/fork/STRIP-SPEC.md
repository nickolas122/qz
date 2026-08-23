# Strip QZ to a trainer bridge — specification

**Status: draft, under discussion.** Nothing here is committed to code yet. Numbers are
measured against the tree at the time of writing unless marked *estimate* or *verify*.

## 1. Goal

Reduce QZ to one job: carry data and control between the FTMS trainer and the four
training apps that matter — **Rouvy, Zwift, Kinomap, MyWhoosh** — with a UI that can be
operated mid-ride, and a settings surface a person can read.

Everything that is not that job is removed from the tree.

### Non-goals

- Supporting anyone else's *trainer*, or anyone else's workflow. Accessories that hang off
  this rider's own bike — fans, shifters, steering, a core-temperature sensor — are kept;
  see §7 Group E for where that line falls and why it is not the same line. Running
  accessories are not kept: this is a bike.
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

One exception, added 2026-08-20: this protects the peripheral role, not every file that
implements it. `virtualtreadmill` publishes a *treadmill*, which nothing in this fork is,
and it comes out with Group E. `virtualbike` — the file the argument above is actually
about — is untouched.

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
| Device drivers | 18 (already cut from upstream's 132); **15 after §7 Group E**, `cscbike` and `stagesbike` among them — see below |
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
  the driver cull is free. *Measured 2026-08-20 (§12 q5): four `static` helpers off the
  class, and the driver is separately the kept cadence sensor. It stays; nothing to break.*

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
`virtualdevices/` minus `virtualtreadmill` (**all platforms including Windows** — §3.2.1,
and §7 Group E for the exception),
`qmdnsengine/`, `qtbluetoothcompat`, `windowsblebond`, `definitions`, `metric`,
`inclinationresistancetable`, `wheelcircumference`, `localipaddress`.

**Platform/infra:** `main`, `logwriter`, `signalhandler`, `keepawakehelper`,
`qzforkversion`, `qzsettings`, the Android glue (`androidactivityresultreceiver`,
`androidqlog`, `androidadblog`, `androidstatusbar`).

**Accessories** (revised 2026-08-20, see §7 Group E): the fans `eliteariafan`,
`fitmetria_fanfit`, `wahookickrheadwind`; the shifting and steering inputs
`sramAXSController`, `cycplusbc2controller`, `thinkridercontroller`,
`elitesquarecontroller`, `elitesterzosmart`, `eliterizer`; and the body sensors
`coresensor`, `moxy5sensor`, `strydrunpowersensor`.

**Fork features:** `gamepadcontroller`, `rtssosd`, `customgears.qml`, `gears.qml`,
`zwift_play/` and `zwift-api/` (decided 2026-08-20 — §12 q6).

**The QZWS WebSocket** (added 2026-08-21, see §7 Group D): `templateinfosender.*`,
`templateinfosenderbuilder.*`, `webserverinfosender.*`, `TemplateWebServer.qml`. This is
how a PC reads the ride — gears, resistance, ERG state — off a tablet running QZ, and how
it shifts back. `tools/qz-rouvy-rtss/` and `tools/xbox-mywhoosh-gears/` are its clients,
and the split-host arrangement of §3.2.1 is the reason it matters. Its wire format is a
published interface now: those two tools parse it, so changing the `workout` broadcast's
field names or the inbound message vocabulary breaks them.

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

**Landed 2026-08-21**, and it took four things with it that the list above did not name,
each because its only remaining reason to exist was a training program:

- `mainwindow.*` and `mainwindow.ui` — the legacy Qt Widgets dialog, reachable only with
  `forceQml` false. Its content was a training-program table and a metrics readout; the
  charts half had already gone in phase 2. `main.cpp`'s `-train` flag went with it.
- `inner_templates/` in full, `webtranslation.*`, and the HTTP static-file half of
  `webserverinfosender` — the transport phase 4 deferred. Its last three consumers were
  `WorkoutEditor.qml`, `TrainingProgramsListJS.qml` and `GoogleMap.qml`, all of them here.
- The second `QZWS` endpoint. `homeform` built two managers, `inner` and `user`; the inner
  one existed to serve those pages and nothing else. There is one endpoint now.
- The training-program half of the template protocol — eleven message handlers for the
  workout editor and browser, plus the GPX, chart and session-array ones that phases 1 and
  2 had already orphaned. `templateinfosenderbuilder.cpp` went from 1,899 lines to 822.

*Not* taken, though the §8 table implies it: `trainprogram_pid_*`. Despite the names those
tune the HR-zone PID, which is driven by `treadmill_pid_heart_*` and survives. The PID's
training-program arm is gone; the setting-driven one is what was always the real feature.

`qthttpserver` and `qHttpServerBin/` also stay, contrary to what §7 Group D predicted. The
`QZWS` WebSocket is served *through* `QHttpServer` — `httpServer->bind(innerTcpServer)`
and `newWebSocketConnection()` — so dropping the module means first porting the socket to
`QWebSocketServer`. That is a rewrite of the one interface two external tools parse, and it
does not belong in the middle of a deletion phase. It is now the only thing standing
between the tree and one fewer vendored Qt module.

### Group D — telemetry, templates and remote control

**Revised 2026-08-21.** The "verify first" note below fired, and the answer split the
group in two. `mqtt/` + `mqttpublisher`, `osc`/`oscpp/`, `QTelnet`, `tcpclientinfosender`,
`TemplateTcpClient.qml` and `templates/` are unreferenced by anything the bridge does and
come out whole. The **WebSocket half of `webserverinfosender` does not** — see below.

Deleted: `mqtt/` (30 files) + `mqttpublisher.*`, `osc.*` + `oscpp/` (9 files), `QTelnet.*`
(already dead — compiled, never constructed), `tcpclientinfosender.*`,
`TemplateTcpClient.qml`, `templates/` (8 files) and the `.qzt` file-template layer that
read it, `inner_templates/floating/` and the floating window it drew.

Kept: `templateinfosender.*`, `templateinfosenderbuilder.*`, `webserverinfosender.*` and
`TemplateWebServer.qml`.

*Verified 2026-08-21, and it did not hold.* There are two RTSS paths, not one. QZ's own
Windows OSD (`rtssosd.cpp`) is self-contained shared memory and touches no template code —
that is the one this note was written about. But [`tools/qz-rouvy-rtss/`](../../tools/qz-rouvy-rtss/)
is an Android-to-PC bridge, and it *does* route through the template system: it reads
`gears`, `resistance` and `autoresistance` out of the periodic `workout` broadcast and
polls `getsettings`, over the `user_QZWS` WebSocket that `webserverinfosender` serves.
[`tools/xbox-mywhoosh-gears/`](../../tools/xbox-mywhoosh-gears/) uses the same socket in the
other direction, sending `gears_plus`/`gears_minus`.

That WebSocket is the only way to read the ride from another machine, and QZ on a tablet
with the training app on a PC is the arrangement §3.2.1 says actually works. It stays, and
it is now a **kept component** rather than a deletion: see §6.

Two things found while verifying, both recorded so the next phase does not rediscover them:

- The `QZWS` endpoint is only created when the template folder scan finds at least one
  **subdirectory** (`load()`, gated on `globalFolderList`). `:/templates/` shipped
  `example/` and `debug/`, and that is the entire reason `user_QZWS` exists. Deleting
  `templates/` without decoupling the endpoint from the folder scan silently removes the
  service. It is now created unconditionally.
- The control signals (`gears_Plus`, `Start`, `Stop`, the rest) were connected in
  `homeform` from the **inner** manager only, whose port is ephemeral — `homeform` rewrites
  `template_inner_QZWS_port` to 0 on every launch. Nothing was connected from the `user`
  manager, so a `gears_plus` arriving on the documented port 6666 was parsed, dispatched,
  emitted and dropped. Route B of `xbox-mywhoosh-gears` cannot ever have shifted a gear.
  Fixed here rather than left, because the whole point of keeping the socket is that the
  PC can both read and drive.

*Not in this group after all, deferred to Phase 3:* the HTTP static-file half of
`webserverinfosender`, and `inner_templates/` less its `floating/` subdirectory. Three live
QML pages — `WorkoutEditor.qml`, `TrainingProgramsListJS.qml`, `GoogleMap.qml` — load their
content from `http://localhost:<template_inner_QZWS_port>/`, and all three are Group C. The
transport cannot come out before its last consumer. When it does, `qthttpserver` and
`qHttpServerBin/` go with it: nothing else in the tree includes `QHttpServer`.

### Group E — rival trainers, and anything that is not for this bike

**Revised 2026-08-20.** This group used to cut the device layer down to `ftmsbike` +
`heartratebelt` + `dircon` and take everything else with it. That was drawn from the file
list rather than from what the files do, and it was wrong: it swept up a dozen drivers that
are not trainers at all.

A driver goes if **either** test fires, and only then:

1. **It competes with the trainer.** Anything that derives from `bike` and *is* the thing
   being ridden. Nobody rides two, so a second one is dead weight the moment the FTMS
   trainer is the only supported machine. Deleting it removes support for somebody else's
   hardware, which §1 already declares a non-goal.
2. **It is for a different sport.** A running pod or a sensor whose readings only mean
   something off the bike. QZ is a bike bridge; a treadmill accessory is somebody else's
   workflow even when the rider owns one.

Everything left over — a fan, a shifter, a steering plate, a sensor worn while riding —
coexists with the trainer rather than replacing it. Deleting one of those removes a
capability from *this* rider and buys nothing.

**Deleted:** `smartspin2k` (676 lines), a rival trainer deriving from `bike`; plus
`moxy5sensor` (320) and `strydrunpowersensor` (912), which are accessories but are for
running rather than for this bike (decided 2026-08-20).

**Kept** — eleven drivers, in four families:

| Family | Drivers | What it is |
| --- | --- | --- |
| Fans | `eliteariafan`, `fitmetria_fanfit`, `wahookickrheadwind` | cooling, driven from the ride's power or heart rate |
| Shifters and steering | `sramAXSController`, `cycplusbc2controller`, `thinkridercontroller`, `elitesquarecontroller`, `elitesterzosmart`, `eliterizer` | gear and grade input, the same category as the kept `gamepadcontroller` and the kept Zwift Play/Click (§12 q6) |
| Body sensors | `coresensor` | core temperature, worn on the bike |
| Power and cadence | `stagesbike`, `cscbike` | the generic BLE power meter and the generic BLE cadence sensor, both read *beside* the trainer |

`elitesterzosmart` and `eliterizer` derive from `bike` despite being accessories; that is
an implementation convenience, not a claim to be the machine, and it is why the base class
cannot be the test on its own.

**`stagesbike` was on the deleted list until 2026-08-20 and should not have been.** It is
two things wearing one class. `bluetooth.cpp` builds it as the machine under
`power_sensor_as_bike` — a power meter riding in place of a trainer, which test 1 catches —
and it builds it *again*, a few branches later, as the power sensor attached to whatever
bike is already connected (`device()->deviceType() == BIKE` in the `power_sensor_name`
pass, feeding `powerChanged` and `cadenceChanged` into the live device). The second is an
accessory in exactly the sense the fans and the shifters are, and deleting the file would
have taken the generic BLE power meter out with it. So the *mode* goes and the driver
stays: `power_sensor_as_bike` and `power_sensor_as_treadmill` are deleted, along with the
`-power-sensor-as-treadmill` switch and the two settings, and what is left of `stagesbike`
is a sensor. This is the third time the group has been drawn from the file list rather than
from what the file does; the rule works, reading only the class name does not.

*Was blocked on:* the `cscbike` dependency in `ftmsbike.cpp` (§4.1). **Resolved
2026-08-20 — see §12 q5:** `cscbike` is not a Group E casualty at all.

**`virtualtreadmill` goes with them** (890 lines). It is the one member of `virtualdevices/`
that §3.2.1 does not protect: that section keeps the peripheral stack because Android and
the Pi need it to publish a *bike*, and nothing publishes a treadmill any more. It is
contained — `dirconmanager.h`'s only mention is a comment, so the DIRCON path is untouched,
and the real callers are `main.cpp`'s `-only-virtualtreadmill` mode, `mainwindow.cpp`, an
`autoInclinationEnabled()` cast in `homeform.h`, and the iOS lockscreen. None of those is
the bridge.

**`treadmill.*` still cannot go, and not for the reason the last revision gave.** Removing
`moxy5sensor` and `strydrunpowersensor` unpins it from them, but `heartratebelt` — one of
the three drivers this whole strip is built around — is declared
`class heartratebelt : public treadmill` (`heartratebelt.h:31`). Until the HR belt is
reparented onto `bluetoothdevice`, `treadmill.*` is load-bearing for a *kept* driver. That
is a two-line change in principle and a behaviour change in practice, so it is named here
and left to Group G to decide.

### Group F — the UI itself

The tile system (91 tiles, 471 settings), `settings.qml`'s 88 sections,
`settings-tiles.qml`, `settings-shortcuts.qml`, `settings-tts.qml`,
`settings-treadmill-inclination-override.qml`, `Wizard.qml`, `profiles.qml`,
`Classifica.qml`, `SwagBag*.qml`, and finally `homeform.cpp` itself.

This is last because everything else must be gone before the replacement UI's scope is
even knowable.

### Group G — other machine types

`rower.*`, `elliptical.*`, `stairclimber.*`, `jumprope.*` and their virtual counterparts,
once no kept driver refers to them. `virtualtreadmill` comes out earlier, with Group E.

**`treadmill.*` is the interesting one.** It is in this group, but only after
`heartratebelt` stops deriving from it (§7 Group E). Reparenting the HR belt onto
`bluetoothdevice` is the precondition, and it is not free: the belt currently inherits a
treadmill's speed and inclination metrics, and whatever reads those has to be checked
before the base class changes underneath it.

*Verify:* `bluetooth.cpp` may switch on these types generically, and the FTMS/DIRCON
characteristic writers (`characteristicwriteprocessor2ad9.cpp` and friends) branch on
`TREADMILL` and `ELLIPTICAL`. Those branches become unreachable rather than wrong, so
removing them is tidying — but it is tidying inside the DIRCON path, which §5 says to
treat as a warning sign. Do it last, or not at all.

## 8. Settings plan

Target: **under 150 keys**, from 1,010. *Estimate*, not a measurement — it falls out of
the deletions above rather than being designed independently.

| Source of deletion | Keys *(approx)* |
| --- | --- |
| Tile system | 471 |
| Machine-specific device panels | ~180 |
| Shortcuts / TTS | ~108 |
| Peloton / Garmin / Strava / training programs | ~75 |
| Remainder: bike, gears, gamepad, virtual device, DIRCON, HR, UI | target ~150 |

The surviving settings should be reorganised around the four things a rider changes —
**bike, gears, training-app connection, display** — not around vendors.

The device-panel row moved from ~200 to ~180 when Group E was revised (2026-08-20).
Measured, not estimated: the ten kept accessories account for **16 keys** —
`elite_rizer_*` (4), `elite_sterzo_smart_*` (3), `fitmetria_fanfit_*` (4),
`shortcut_fan_±` (2), and one apiece for `cycplus_bc2_controller`, `sram_axs_controller`
and `thinkrider_controller` — plus `zwift_play`, `zwift_click` and `zwift_play_vibration`,
and two `tile_coretemperature_*` keys that belong to Group F either way. Dropping
`strydrunpowersensor` took `stryd_*` (3) with it. So the under-150 target survives the
revision comfortably, which is the answer to the obvious worry about keeping ten more
drivers.

Those keys do not fit the four groups above. A fifth, **Accessories**, is the honest
answer at Phase 6 rather than forcing a fan into "display".

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
| 3 | Group C — training programs | medium (homeform surgery) | **none** |  ← landed 2026-08-21, ahead of 6
| 4 | Group D — telemetry | medium (verify RTSS first) | **covered** |  ← landed 2026-08-21; RTSS check failed, see §7 Group D
| 5 | Group E — rival trainers, running sensors, `virtualtreadmill` | low, once cscbike is resolved | **covered** |  ← landed 2026-08-20, H1 passed 2026-08-21
| 6 | Settings consolidation | medium (§3.6 runtime failures) | **covered** |  ← moved to after 7c, see below
| 7a | `RideState` object + `ui_next` flag + new tree under `src/ui/` | medium | **none** |  ← landed 2026-08-21
| 7b | Ride on the new UI with Rouvy and Zwift; flip the default | low, but needs calendar time | **none** |  ← default flipped 2026-08-23 on H3; H4 still owed, see §11.6
| 7c | Delete Group F — old tree, tile system, `homeform.cpp`, the flag | high | **none** |  ← split 7c-1/7c-2; 7c-1 landed 2026-08-23, 7c-2 gated on H4
| 8 | Group G, Pi build revival | medium | partial |

"Covered" means the end-to-end loop asserts on that phase's blast radius: bike frames in,
metrics, DIRCON, a client reading numbers back out. It is deliberately **not** UI coverage
— `homeform` cannot be constructed without a QML engine, so the loop asserts on the wire
rather than on tiles. That is the whole of the distinction in the column.

Which argues for taking the covered phases first. The numbering here is from the original
plan and the §11.6 criteria refer to it, so it stays; but **1, 2, 5, 4, 6 before 3 and 7**
spends the safety net where it exists and arrives at the UI work — the part that has to be
validated by riding — against a much smaller tree.

**Corrected 2026-08-21.** That order put 6 before 3, and 6 cannot reach its target there.
§8 sources 361 of the keys it has to delete from the tile system, the shortcuts and TTS
— all group F, all held until 7c — and §9.6's four groups describe a settings
*screen* that does not exist until 7a. Phase 6 is a consolidation of what survives, so it
belongs after the things that do not. The order actually taken is **1, 2, 5, 4, 3**, with 6
moved to sit after 7c.

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
| `bluetoothdevicetestsuite` + `devicetestdataindex` | data-driven device discovery | **Keep**; prune the two rival-trainer rows (Phase 5) |
| `ergtabletestsuite`, `TestErgTableSelection`, `TestErgAutoMode` | ERG tables | Keep unless `ergtable` is cut |
| `TestZwiftRideController` | Zwift Play/Click | **Keep** — open question 6 resolved kept |
| ~~`garminconnecttestsuite`~~ | Garmin | **Deleted**, Phase 1 |
| ~~`qfittestsuite`, `testtrainingloadtestsuite`~~ | FIT writing, training load | **Deleted**, Phase 2 |
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

Two checks live outside the suite because they need the shipped executable rather than the
library, and process orchestration is where flakiness comes from:

| Script | Covers | Needs |
| --- | --- | --- |
| `tools/dircon_smoke.py` | the shipped binary announcing and serving DIRCON, discovered with a foreign mDNS stack | QZ running |
| `tools/qzws_smoke.py` | the QZWS WebSocket: the `workout` broadcast, `getsettings`, and a shift arriving from outside | QZ running **with its UI** |

Neither is in CI. `qzws_smoke.py` cannot be: the template managers are built in the
`homeform` constructor, so there is no QZWS under `-no-gui` at all, and that is the same
wall §11.1 hits everywhere else.

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
*Done 2026-08-16* — `tst/Devices/TestGearTable.*`.

**2. Grade→resistance test** *(gtest)* — pin the formula
`grade% × 1.5 + bike_resistance_offset + 1 + CRR + CW`, so the flat-terrain calibration
(offset 13) cannot silently regress.
*Done 2026-08-20* — `tst/Devices/TestGradeToResistance.*`, driving
`CharacteristicWriteProcessor2AD9` both directly and through a whole 0x11 frame off the
wire. Two things it pins that were not obvious from the formula as written: the
`zwift_inclination_gain`/`offset` pair steers the *displayed* grade and never the
resistance, and `CW_offset` reads the rolling-resistance byte rather than the wind one
(recorded in [TODO.md](TODO.md); pinned as-is because correcting it changes how a gravel
sector feels for anyone who turned the gains up).

**3. Settings-integrity check** *(script, runs in CI)* — three assertions: `allSettingsCount`
equals the declared count; catalog `settingCount` equals its array length; and **every
setting name referenced in any `.qml` resolves to a key in `qzsettings.h`**. The third is
new and is what makes mass deletion safe.
*Done 2026-08-16* — `tools/check-settings-integrity.py`, its own CI job with no Qt and no
build. Twenty-five pre-existing QML-only names are baselined, so the check fails when that
list grows.

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
*Status:* **done 2026-08-20**, in three commits — Garmin Connect, Intervals.icu, then
Strava with Peloton, PowerZonePack and HomeFitnessBuddy together, which had to go as one
because they shared the OAuth plumbing and the workout-name fields. `allSettingsCount`
1012 → 961 and the catalog 976 → 931, so **51 keys**, close to the ~55 estimated.

The estimate that mattered more was the coupling one, and it held: nothing under
`devices/` or `virtualdevices/` was touched, and no file in the bridge appears in the
diff. The one edit that reached the FIT path was passing empty strings where Peloton
used to supply a workout id and URL.

Three groups of names carry a deleted vendor and are **not** this feature; each was
checked before it was kept:

| Kept | What it actually is |
| --- | --- |
| `peloton_gain`, `peloton_offset`, `peloton_heartrate_metric`, the `Peloton R(%)` tiles, `ss2k_peloton`, `ios_peloton_workaround`, `tacx_neo2_peloton`, the `*_peloton_formula` settings | the Peloton **resistance scale** — a unit conversion the bridge does, plus the virtual device's metric override |
| `peloton_workout_ocr`, `peloton_bike_ocr`, `peloton_companion_workout_ocr`, `trainprogram`'s `pelotonOCR*` | screen-reading sync, an unrelated feature not named in Group A |
| `garmin_companion`, `android/src/Garmin.java`, `ios/GarminConnect.swift`, `garmin_bluetooth_compatibility`, `fit_file_garmin_device_*` | the ConnectIQ watch companion and FIT metadata, neither of which talks to connect.garmin.com |
| `strava_virtual_activity`, `strava_treadmill`, `powr_sensor_running_cadence_half_on_strava` | how `qfit` writes the file; they go with Phase 2 |

Two pieces of residue are deliberate and named here so the next phase does not have to
rediscover them:

- `homeform`'s `stravaPelotonActivityName`, `stravaPelotonInstructorName`,
  `stravaWorkoutName` and `stravaPelotonWorkoutType` survive: the first, third and
  fourth still carry the ZWO/GPX workout's name and sport into the FIT file. The
  instructor is now always empty. Renaming them would churn a file Phase 7c deletes
  whole, so they keep their vendor names for now.
- `inner_templates/floating/*.htm` and `chartjs/dochart.js` still send
  `peloton_start_workout`, `peloton_abort_workout` and `getpelotonimage` over the
  template channel. Nothing answers them any more, which is a no-op rather than a
  fault; the whole directory is Group D and comes out in Phase 4.

Removed with them, because nothing else used either: Qt's `networkauth` module (the
only OAuth client in the tree was this group) and `qtoauthcompat.h`, which existed to
paper over its Qt 5/6 signature change.

**Phase 2 — recording**
*Criteria:* FIT writing, history, charts, e-mail report and `smtpclient/` gone.
`qfittestsuite` and `testtrainingloadtestsuite` deleted; `tst/test-artifacts/*.fit` and
`*.sqlite` removed.
*Tests:* as Phase 1. Confirm the app starts and rides with no session storage — the
absence of a writer must not fault the metric pipeline.
*Hardware:* **none.**
*Status:* **done 2026-08-20.** 340 files deleted, ~93,600 lines — the largest single
deletion the strip will make, and almost all of it vendored: `fit-sdk/` is 314 files and
84,900 lines on its own, and `smtpclient/` was a submodule checked out by six separate CI
steps that are now gone too.

Settings: `allSettingsCount` 961 → 952, catalog 931 → 923. Only nine keys, which is the
surprise of the phase — recording was enormous in code and nearly free in configuration.

**`Session` stays.** §7 lists the writers and the viewers, not the in-memory ride, and the
distinction turned out to be load-bearing: `metric::calculateVO2Max()` and
`metric::powerPeak()` take a `QList<SessionLine>*`, both live in the kept `metric.*`, and
both feed tiles. `sessionline` is confirmed kept for the same reason — one of §6's
"verify before assuming kept" entries now settled.

Four things came out that §7 does not name, each because its only caller was in the group:

- The **RPE/feel popup**. It existed to embed two numbers in the FIT file before writing
  it. With no file to write, `finalizeFitSave()`, `rpe_feel_popup_enabled` and the popup
  itself are pointless. `Stop()` no longer defers anything.
- The **gzip helpers** (`crc32ForGzip`, `appendLittleEndian32`, `gzipCompress`). They
  compressed the debug log for the e-mail attachment. File-static, so leaving them would
  have started warning.
- `TemplateInfoSenderBuilder::previewSessionOnChart()`, which had no caller left once
  `homeform::fitfile_preview_clicked()` went, and which was the last thing outside the
  group holding an `#include "fit_profile.hpp"`.
- `mainwindow.cpp`'s Charts window, and `main.cpp`'s `-fit-file-saved-on-quit`.

**Qt Charts stays**, which is worth stating because it looks like it should have gone.
`TrainingProgramsList.qml` draws the workout *preview* with it, so `update_chart_power()`,
`wattMaxChart()` and `qtchartscompat.h` survive to Group C. `update_chart_heart()` and
`save_screenshot_chart()` had no caller outside the deleted end-of-workout screen and went.

Two settings that read as Group A residue are gone here instead, because `qfit` was their
only reader: `strava_virtual_activity`, `strava_treadmill` and the half-cadence switch
decided how the FIT file was *written*, not where it was uploaded. Same for
`fit_file_garmin_device_training_effect*` and `garmin_device_serial`, which wrote a Garmin
product ID into the file. `powr_sensor_running_cadence_double` is **not** one of these —
`characteristicnotifier2ad2` reads it, so it is bridge and it stays.

**Phase 3 — training programs**
*Criteria:* `trainprogram`, ZWO, GPX, KML, video and maps gone; `trainprogramtestsuite` and
`zwiftworkouttestsuite` deleted; `homeform.h` no longer declares `trainProgram` or
`previewTrainProgram`.
*Tests:* as above, **plus the DIRCON loopback test (§11.5 item 5)** — this is the first
phase large enough to plausibly disturb the control path, and that test is what makes
skipping a hardware ride defensible.
*Hardware:* **none, if item 5 exists.** If it does not, this phase needs a ride — which is
the argument for building it first.

*Landed 2026-08-21.* Taken out of order, ahead of phase 6: §8's under-150 target sources
361 of its keys from the tile system, the shortcuts and TTS, all of which are group F and
cannot come out before phase 7c, so phase 6 in its numbered slot could only have produced a
tidy-up that 7c then deletes. See §7 Group C for the four things this phase took beyond its
own list.

The criteria held, and the two harnesses §11.6 leans on carried it: the gtest suite ran 180
green with the same 12 pre-existing skips (192 from 19 suites, down 9 with the two deleted
ones), and `tools/qzws_smoke.py` was green against the shipped binary on a simulated bike
before and after — broadcast, `getsettings`, and a shift arriving from outside still move
the gear. `homeform.cpp` lost 1,000 lines, `settings.qml` 513, and 19 more settings keys
went in all four places.

One measurement worth keeping: `update()` alone held 80 of the 327 `trainProgram`
references, spread across the pace, power-zone, HR-PID and peloton tiles. The guards came in
two shapes — `if (trainProgram)` blocks, which delete, and `if (!trainProgram || …)` guards,
which are now always true and had to be *unwrapped* rather than removed, keeping the branch
that used to be the no-program fallback. Getting that backwards would have silently deleted
the surviving behaviour instead of the dead one.

**Phase 4 — telemetry and templates**
*Precondition:* **checked 2026-08-21, and it failed.** The overlay does route through the
template system — see §7 Group D for what that turned up and how the group was re-drawn.
The webserver info sender is therefore **kept**, not deleted, and the phase's criteria are
the revised ones below.

*Criteria:* MQTT, OSC, telnet, the TcpClient template and the `.qzt` layer, `templates/`,
and the floating window gone. The `QZWS` WebSocket still serves the `workout` broadcast and
still answers `getsettings`, on a port that survives a restart. The control vocabulary
(`gears_plus`/`gears_minus`, `resistance_*`, `speed_*`, `inclination_*`, `start`, `pause`,
`stop`, `lap`, `autoresistance`) reaches `homeform` from the `user` endpoint, which it did
not before. `homeform` no longer declares `floatingOpen` or `openFloatingWindowBrowser`.

*Tests:* the universal gates of §11.4, plus a WebSocket loopback check standing in for the
two Python tools: connect to `user_QZWS`, assert the broadcast carries `gears`,
`resistance` and `autoresistance`, assert `getsettings` answers `zwift_erg`, and assert a
`gears_plus` arriving on that socket moves the gear. This is the same argument §11.5 item 5
makes for DIRCON — it converts "ride and look at the overlay" into something CI can run.

*Hardware:* **none.** The bridge tools are exercised against the loopback socket; nothing
in this phase touches the BLE path.

**Phase 5 — the rival trainers and the running sensors**
*Criteria:* `smartspin2k`, `moxy5sensor`, `strydrunpowersensor` and `virtualtreadmill`
gone, with `-only-virtualtreadmill` and `homeform`'s `autoInclinationEnabled()` cast gone
with them; `stagesbike` kept as the power sensor, with `power_sensor_as_bike` and
`power_sensor_as_treadmill` deleted so it can no longer be the machine (§7 Group E); the
`cscbike` dependency in `ftmsbike.cpp` resolved (open question 5); the accessories of §7
Group E untouched and still discovered; `bluetoothdevicetestsuite` data pruned of the
removed rows and still green. `treadmill.*` **stays** — `heartratebelt` derives from it.

*Landed 2026-08-20*, less two deferrals worth naming rather than discovering later:
`stryd_speed_instead_treadmill`, `stryd_inclination_instead_treadmill` and
`stryd_add_inclination_gain` are still declared, because their only readers are inside
`treadmill.cpp`, which Group G deletes whole; and `cadence_sensor_as_treadmill` survives
for the same reason. Both are inert — the sensors that fed them are gone — and both cost
one line each to remove with the file that reads them.
*Tests:* device-discovery suite green against the reduced set; FTMS handshake and slew
limiter suites unchanged and green.
*Hardware:* **yes — H1. Done 2026-08-21, and it passed.** ELITE AVANTI (`YPBM001264`)
connected over the Qt 6 Windows build, gears exercised 1 through 16, resistance tracked
10 through 26 across 20 commands, 38 frames of Indoor Bike Data in 59 seconds. The
name-matching cascade this session existed to check handed the trainer to `ftmsbike` as
before — which is the whole question §11.6 asked of it.

The session also turned up a Windows connection fault that has nothing to do with the strip:
the first launch found the bike and then never finished discovering its services, so nothing
was subscribed and the trainer never started. It recovered by itself on the next run. Written
up in [TODO.md](TODO.md); not a phase 5 defect and not a gate on anything here.

The revision of Group E made this phase much smaller — four drivers rather than fourteen —
but it did **not** make it safe to skip H1. `bluetooth.cpp` decides which driver to build
from a name-matching cascade, and removing a branch from the middle of that cascade is
exactly the change that can hand the trainer to the wrong class. The hardware session is
justified by the cascade, not by the line count.

`virtualtreadmill` is the one item here that touches `virtualdevices/`, which §3.2.1
otherwise protects wholesale. Worth a second look at the Android build in particular: the
peripheral stack is compiled unconditionally, and this is the first time anything is taken
*out* of it.

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

*Landed 2026-08-21.* `src/ui/` holds `ridestate.{h,cpp}` and nine QML files: `Main.qml`
with the three-tab bar, the three screens, and four small components. `main.cpp` picks the
entry QML off the flag and exposes `rideState` as a context property; nothing else changed
in the old path.

`RideState` came out at **16 members** — 12 properties and 4 invokables, the §9.2 list
exactly — and `TestRideState` asserts the member names rather than just the count, so
adding one fails the test even if the total stays under the ceiling. Nine cases, all green.

Three things worth recording:

- **`appName` is empty, always.** A BLE central never announces who it is, and the DIRCON
  client's address is known only to `DirconProcessor`, which does not surface it. Reaching
  it means widening the bridge to satisfy a status pill, which is the trade §9.2 exists to
  refuse. The pill shows the transport instead — "Training app (DIRCON)" — which is
  the half that tells you something when a ride stops. One accessor was added:
  `virtualbike::isDirconFTMS()`, so the pill can name the path rather than guess it.
- **`homeform` still runs behind the new UI.** It has to: `buildContext()` reads
  `homeform::singleton()` for most of the QZWS broadcast, and that socket is what
  `tools/qz-rouvy-rtss` draws from. Its QML wiring is now guarded on `home` — the old
  tree's tile grid, whose presence *is* the test for which tree loaded — because
  connecting to a root object that has none of those signals was 36 warnings and no
  behaviour. **Phase 7c cannot delete `homeform` until the broadcast is re-pointed**, at
  `RideState` or at the device directly. That is the real prerequisite hiding in 7c.
- The **settings screen holds the four groups, not the final controls.** Which of the
  surviving keys belong on that page is phase 6's question, and phase 6 now runs after 7c.
  What is there is what a rider reaches for mid-setup; the shape is what 7a was for.

*Verified:* contract test 9/9; full suite 189 green with the same 12 skips (201 from 20
suites, up 9 with the new one); Qt 5 `qmllint` clean on all nine files; the Qt 6 import
rewriter picks all nine up and leaves their imports alone, as §9.8 predicts. With the flag
on the new tree loads with **zero warnings** and `qzws_smoke.py` is still green through it;
with the flag off the old tree logs 51, all of them pre-existing categories and 18 fewer
than before phase 3. Android is unverified locally — §3.5's wall — and is CI's job.

**Phase 7b — flip the default**
*Criteria:* the new UI has ridden successfully with both Rouvy and Zwift.
*Tests:* no automated substitute exists. This is an acceptance test by definition.
*Hardware:* **yes — H3 and H4.** One ride per app. Gear display and shifting correct,
connection status accurate, ERG toggle behaves, nothing unreadable from the bars.

*Default flipped 2026-08-23, on H3 alone.* `default_ui_next`, `settings.qml` and the
catalog entry all now say true, and the new tree is what loads on a fresh install. The
Rouvy ride passed; **H4 has not been run**, so the criterion above is half met and this
phase is not closed. The flip is still the right move on half the evidence — it is what
puts the new tree in front of the next ride rather than behind a toggle nobody
remembers to set — but that argument only holds while the toggle back exists.

So it was made reachable. §9.9 promises a bad ride costs a settings toggle rather than a
rebuild, and with the default flipped the *new* Settings screen was the one place that
promise could still be kept from; the old tree's Experimental page is no use to a rider
who can no longer get to it. `SettingsScreen.qml` grew a "Use the old UI" switch under
Display, inverted against `ui_next` so the label reads as the thing it does.

**This is the gate on 7c.** That phase deletes the flag and the tree the switch returns
to, and at that moment Zwift has no fallback left. H4 runs before the deletion commit
lands, not before its preparation — the 7c entry below says which half is which.

Outstanding new-UI tweaks are not blockers and are not tracked here; they are in
[TODO.md](TODO.md), including the display-cutout entry from 2026-08-23, which is an
upstream layout fault the new tree inherits rather than a strip regression.

**Phase 7c — delete the old UI**
*Criteria:* `homeform.cpp`, the tile system, `settings.qml`'s 88 sections and the
`ui_next` flag all gone; no QML references a deleted component.
*Tests:* full suite; QML lint; headless smoke. Gate 4 does the heavy lifting.
*Hardware:* **none** — 7b already validated the replacement, and this phase only removes
the thing it replaced.

**Split in two, 2026-08-23.** The hardware note above assumed 7b was complete. It is not
— H4 has not run (§11.6 phase 7b) — and 7c is where the fallback disappears. So the phase
is now two commits with the ride between them:

- **7c-1, decouple.** Nothing is deleted. Everything in the kept tree that reaches into
  `homeform` stops doing so, and the old UI keeps working throughout. *Landed 2026-08-23.*
- **7c-2, delete.** The old tree, the tile system, `homeform.{h,cpp}` and the flag.
  **Gated on H4** — the "Use the old UI" switch is what makes an unridden Zwift survivable,
  and this commit removes it.

*7c-1, landed 2026-08-23.* 7a called the QZWS broadcast "the real prerequisite hiding in
7c". Measuring it found four couplings, not one, and three were smaller than feared:

| Coupling | Sites | What it was, and where it went |
| --- | --- | --- |
| `setToastRequested` | 24 | A driver with something to say - a battery level, "restart to apply", "another device has the bike" - reaching for the UI class and null-checking it. Now `QzNotify::toast()`, a sink both trees attach to and neither owns |
| `updateGearsValue` | 1 | `bike.cpp` poking homeform to refresh the gear *tile*. `RideState` polls `gears()` off the bike, so the poke went with the tile |
| `*_color` ×11 | 11 | Tile font colours in the broadcast — `pace->valueFontColor()` and friends. No consumer of the socket reads them; deleted |
| `autoresistance` | 1 | **Kept.** `tools/qz-rouvy-rtss` reads it (§7 Group D). It turned out `bluetoothdevice` has carried the same flag all along, and homeform was already mirroring into it on every toggle, so the broadcast just reads the device now |

The last row is the only one that can report a different number than it did before, and
it reports a better one. The two flags are not always equal: `ftmsbike.cpp` clears
`autoResistanceEnable` outright for ICSE bikes, and homeform never hears about it. The
device's flag is the one `bike::changeResistance` actually gates on, so it is the honest
answer to "is QZ driving resistance" — which is the question the tool reading this field
is asking. On an ICSE bike the broadcast used to say true while the bridge ignored every
resistance request. It no longer does. Not this bike (§1), so nothing here rides on it.

The result: **`src/devices/` no longer references `homeform` at all** — seven driver files
lost the include outright, four more were including it without using it, and
`bluetooth.cpp` keeps six mentions that are all comments. `templateinfosenderbuilder.cpp`
is clean, and its `homeform::singleton()` null-guard — the early return that would have
silenced the whole broadcast — is gone with the dereferences it protected.

Three things worth recording:

- **`QzNotify` is a sink, not a UI.** `toast()` is static, takes no null check and returns
  nothing, so a driver posting into a process with no UI loaded — the headless smoke, the
  gtest suite — costs an `emit` to nobody. That is the property that let 24 call sites lose
  their guard rather than move it.
- **The new tree got its own `ToastArea.qml`** rather than importing the old tree's
  `Toast.qml`/`ToastManager.qml`, which are kept and would have worked. 40 lines against
  156, and it does not inherit `AndroidStatusBar`, whose height is 0 for the life of the
  process (TODO.md, 2026-08-23). Without it the new UI would have gone silent on every one
  of those 24 messages the moment homeform stopped relaying them.
- **`bluetooth::homeformLoaded` is now `uiLoaded`.** Not a dependency — a name — but
  leaving it would have left the bridge gating discovery on a flag named for a class that
  no longer exists.

*Left for 7c-2, found while doing this.* Three things, none of them a deletion:

1. **`uiLoaded` is still set from `homeform.cpp`**, so the desktop discovery gate depends
   on homeform being constructed even with `ui_next` on. The new tree has to set it
   itself.
2. **`autoResistance` has no toggle outside the old UI.** The QZWS command routes through
   `homeform::toggleAutoResistance`, and after 7c-2 nothing would receive it. `RideState`
   takes it over — its 17th member, still inside the 20 the contract test allows.
3. **Three static file-location helpers live on `homeform`** and are called from
   `main.cpp` before any UI exists: `getWritableAppDir()`, `getProfileDir()` and
   `loadSettings()`. They are not UI at all — they answer "where does this platform let
   QZ write" — and they are the last thing in `main.cpp` that needs the class. They move
   to a kept home rather than being deleted.

`main.cpp` is now the only file outside `homeform.{h,cpp}` and the iOS shims that calls
into the class at all. Everything else that did is either gone or reading a comment.

*Verified:* mingw debug build clean; suite 189 passed, 12 skipped, unchanged; Qt 5
`qmllint` clean on all ten new-UI files; settings integrity consistent; the app starts,
loads the new tree and logs **zero** QML warnings, which is the same signature 7a
recorded — the old tree logs 51.

**Phase 8 — other machine types, Pi revival**
*Criteria:* treadmill/rower/elliptical/stairclimber/jumprope gone with no kept driver
referring to them; the Pi jobs re-enabled and green.
*Tests:* full suite; the Pi smoke-test job.
*Hardware:* Pi only, and only when that target is actually pursued.

### 11.7 Hardware budget

Four sessions for the whole project:

| | After | Purpose |
| --- | --- | --- |
| **H1** | Phase 5 | Trainer connects; gears 1–15 shift; resistance tracks the table. Add any accessory the rider owns to the same session — the kept ones (§7 Group E) have no automated coverage at all |
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
5. ~~**`cscbike`** — what does `ftmsbike.cpp` actually use it for?~~ **Resolved
   2026-08-20: the custom resistance→power table, and `cscbike` stays regardless.**
   `ftmsbike.cpp` calls four `static` members — `clampedCustomResistance`,
   `customResistanceAdjustedWatts`, `customResistanceMax` and
   `useCustomResistancePowerTable` — which are table lookups that happen to live on that
   class and never touch an instance of it. Separately, `cscbike` is what
   `bluetooth.cpp` builds for the `cadence_sensor_name` branch: it *is* the generic BLE
   cadence sensor, an accessory that coexists with the trainer, so Group E's rule keeps
   it on its own merits. Both readings agree, and neither needs the statics moved.
6. ~~**Zwift Play / Click** controllers — kept or cut?~~ **Resolved 2026-08-20: kept.**
   They are a shifting input, and Group E settled that shifting inputs survive the cull;
   cutting these while keeping `sramAXSController`, `cycplusbc2controller` and
   `thinkridercontroller` would have needed an argument nobody made.
   `TestZwiftRideController` (§11.1) is therefore a keeper.

   One thing to measure before Phase 5 rather than after: `zwift_play/` is 1,945 lines and
   self-contained, but `zwift-api/` is 7,084 — mostly `zwift_messages.pb.*`, generated
   protobuf, which is also what drags the protobuf dependency into the Windows link
   (see the `-llibprotobuf` line in `tst/qdomyos-zwift-tests.pro`). If the controllers only
   need part of that, the rest is still a deletion candidate; keeping the *feature* does not
   commit the fork to keeping all 7,084 lines. That is a measurement, not an open decision.
