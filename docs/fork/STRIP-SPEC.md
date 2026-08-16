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

Recorded as a candidate follow-on, gated on the transport tests in §11. If Rouvy, Zwift
and Kinomap all reach QZ over DIRCON from Windows, it is a failsafe for a path that never
fails and should never be built.

### 3.2.2 Kinomap is Android-only, and best-effort

Established 2026-08-16: Kinomap has no Windows client at all, so it places **no
requirement on the Windows build** and does not affect §11's Windows question.

The intended topology is Kinomap and QZ on the same Android device. That is unlikely to
work: Android's Bluetooth adapter does not receive its own advertisements, so a BLE
central on the device cannot discover or connect to a GATT server published by that same
adapter. Same-device bridging would require Kinomap to accept DIRCON over localhost, and
Kinomap is a BLE/ANT+ trainer client.

**Declared best-effort by the rider** — if it does not work, it is not a defect and not
worth engineering time. Realistically it needs a second device or the future Pi. No phase
in §10 is gated on it and no design decision should be bent toward it.

### 3.2.3 Confirmed topology

Measured, not assumed. All four apps accounted for as of 2026-08-16:

| App | Host | Reaches QZ via | Status |
| --- | --- | --- | --- |
| Rouvy | Windows | DIRCON | **Confirmed working** |
| Zwift | Windows | DIRCON | **Confirmed working** — discovered as "ELITE AVANTI" |
| MyWhoosh | Windows | DIRCON, from QZ on Android/Pi | Works; same-host impossible (§3.3) |
| Kinomap | Android | BLE | Best-effort, likely needs a 2nd device (§3.2.2) |

The conclusion that matters for scope: **no app that runs on Windows needs the BLE
peripheral role.** Windows is fully served by DIRCON, which this fork has already
hardened. Windows keeps its place, and §3.2.1 stays out of scope with no remaining
argument against it.

### 3.3 MyWhoosh will not talk to a DIRCON peer on its own IP

Confirmed, and upstream ([issue #3314](https://github.com/cagnulein/qdomyos-zwift/issues/3314)).
QZ and MyWhoosh cannot share one machine. MyWhoosh therefore requires QZ on Android or
on the Pi — it can never be served by QZ on the same Windows box.

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

*Sketch only; this section is the least settled and should be the focus of the next
session.*

The mid-ride requirement drives it: while riding, the training app owns the screen. QZ's
UI is therefore consulted **before** a ride and glanced at **during** one, and those are
different needs.

**Screen 1 — Ride.** Connection state (trainer / training app), current gear, resistance,
power, cadence, HR, ERG on/off. Large enough to read from the bars. This is roughly what
the tile system delivered, minus 85 tiles nobody uses.

**Screen 2 — Setup.** Trainer pairing, virtual-device mode (BLE or DIRCON), discovery
status, the gear table.

One requirement falls out of §3.2.1: on Windows the setup screen **must not present the
BLE virtual device as a working option**. The code is compiled in but cannot advertise,
and an enabled-looking toggle that silently does nothing is exactly the kind of UX this
project is trying to remove. Show it as unavailable on this platform, with the reason.

**Screen 3 — Settings.** One page, grouped as §8.

Open: whether the ride screen is worth building at all on Windows, given the RTSS overlay
already puts gear/ERG/resistance on top of the fullscreen training app, and QZ is behind
it. On Android — where the tablet may be visible next to the bars — it clearly is.

## 10. Phasing

Each phase must end with a tree that **builds on both Windows and Android** and **rides
with Rouvy**. That is the acceptance test for all of them; there is no unit-test safety
net here.

| Phase | Content | Risk |
| --- | --- | --- |
| 0 | Rewrite `FORK.md` §Versioning (§3.1); tag a pre-strip release as the fallback | none |
| 1 | Group A — leaf integrations | low |
| 2 | Group B — recording | low |
| 3 | Group C — training programs | medium (homeform surgery) |
| 4 | Group D — telemetry | medium (verify RTSS first) |
| 5 | Group E — drivers | low, once cscbike is resolved |
| 6 | Settings consolidation | medium (§3.6 runtime failures) |
| 7 | New UI, then delete Group F | high |
| 8 | Group G, Pi build revival | medium |

Phase 0 matters more than it looks: once phase 1 lands, there is no going back to
upstream. A tagged release beforehand is the only rollback.

## 11. Open questions

1. ~~**Kinomap's transport.**~~ **Resolved 2026-08-16:** Kinomap has no Windows client,
   so it imposes nothing on the Windows build. Reduced to a best-effort Android case —
   see §3.2.2.
2. ~~**Zwift's transport from Windows.**~~ **Resolved 2026-08-16:** Zwift connected to
   QZ over DIRCON from Windows, discovering it as "ELITE AVANTI". See §3.2.3.
3. ~~**Does the Windows build survive?**~~ **Resolved 2026-08-16: yes.** Windows serves
   both apps that run on it, over DIRCON. It stays a first-class host, the §10 phasing is
   unaffected, and the deferred WinRT peripheral work (§3.2.1) stays buried.
4. **Heart rate:** is the HR belt used, or does the training app read it directly?
5. **`cscbike`** — what does `ftmsbike.cpp` actually use it for?
6. **Zwift Play / Click** controllers (`zwift_play/`, `zwift-api/`) — kept or cut? They
   are a shifting input, which overlaps with the gamepad work.
