# The virtual bike

**Draft — spec, not yet implemented.**

A way to exercise QZ without the trainer in the room: a simulated bike the app can run
against, and a harness that feeds the *real* `ftmsbike` byte-exact FTMS frames and reads
back the bytes it writes. One scenario format serves both.

## The problem

The Bluetooth link to the bike works and data streams — that much is established, and it
is exactly why it is no longer where the risk sits. The risk has moved to everything
downstream of the first correct packet: what the tiles show, what the gear table
computes, what ERG asks for, what goes out over DIRCON, what lands in the FIT file. All
of that currently requires the bike to be powered, in range, and not held by another
central. Any change to it is verified by getting on a trainer, which means it is verified
rarely, late, and only for the one path the rider happened to ride.

The test suite does not cover the gap. It covers device *detection* (name patterns and
settings, `tst/Devices/bluetoothdevicetestsuite.cpp`) and four extracted policy classes —
the control point handshake, the resistance slew limiter, the service subscription plan,
the gear table. Those exist precisely because the fork's habit, when Qt-coupled code got
something wrong twice, was to lift the *decision* out into a plain class and test that.
It works, and it should continue. But it leaves the code that reads a 0x2AD2 frame and
turns it into a speed — `ftmsbike::characteristicChanged`, roughly 600 lines from
`ftmsbike.cpp:971` — with no test at all, in either direction: nothing exercises the
parse, and nothing asserts on the bytes QZ writes back to the control point.

## What is actually there today

Worth stating plainly, because two of these are traps:

**Upstream's fake devices are gone.** `fakebike`, `faketreadmill`, `fakeelliptical` and
`fakerower` were deleted in `2c39c5d` ("Phase 2: one bike, its sensors, and nothing
else"), along with the synthetic `deviceDiscovered()` in `bluetooth::finished()` that used
to bring them up and the 15-second discovery watchdog that unstuck them.

**The "Fake Device" toggle is still in the UI and does nothing.** `settings.qml:16324`
still offers it, and `applewatch_fakedevice` still round-trips to `QSettings` — but no
code path in this tree instantiates a device from it. Its only surviving effect is that
`homeform::firstRun()` (`homeform.h:412`) stops offering the wizard. The treadmill,
elliptical and rower variants below it are in the same state. Someone following
`docs/android-testing/README.md`, which records the emulator as configured with "Fake
Device and Android Notification enabled", will turn it on and watch nothing happen. That
doc is stale for this fork and needs a line once this lands.

**The existing captures are not this bike.** `btlogs/` holds upstream's btsnoop
captures — Domyos hardware, a different protocol path. They are not a fixture source
for the FTMS trainer this fork is built around.

**QZ already logs every frame in both directions.** `ftmsbike::characteristicChanged`
logs `uuid`, length and `<< hex` at `ftmsbike.cpp:999`; `processWriteQueue()` logs
`>> hex // info` at `ftmsbike.cpp:172`. Every debug log from a real ride is already a
recording of the session, in this fork's own format. This is the cheapest fixture source
available and the spec leans on it.

**CI runs the suite in exactly one job.** `linux-x86-build` in `.github/workflows/main.yml`
builds Qt 5.15.2 on ubuntu-24.04 and runs `tst/qdomyos-zwift-tests` under Xvfb. Anything
specified here that is meant to catch a regression has to run there.

## What this is not

- **Not a radio.** Discovery, the WinRT backend, bonding, pairing refusal and reconnect
  backoff are not exercised by anything below. They need a real peripheral on a real
  radio, and that is deferred — see [Deferred: a real peripheral](#deferred-a-real-peripheral).
- **Not a resurrection of the device zoo.** One bike. No fake treadmill, rower or
  elliptical; those base classes have no driver in this fork and simulating them would be
  simulating dead code.
- **Not a replacement for riding the bike before a release.** It shortens the loop; it
  does not close it. Anything that only fails against real hardware still only fails
  against real hardware.

## Shape: one scenario, two players

The spine of the design is that a **ride scenario** is one file, and two different things
can play it:

```
      tst/fixtures/rides/*.ride
                 │
      ┌──────────┴───────────┐
      │                      │
  Layer A                Layer B
  simulatedbike          encode to FTMS frames
  (drives metrics        → real ftmsbike parser
   directly, in-app)       (in tst, headless)
      │                      │
  tiles, gears, ERG,     metrics + the bytes
  DIRCON, FIT            QZ writes back
```

A scenario is a plain, readable, timestamped list of what the rider is doing:

```
# ramp.ride — 60s ramp from soft-pedal to threshold, steady cadence
mode        power            # what the file asserts on: power | resistance | speed
resistance  12               # starting resistance level, if the bike reports one
t=0     watts=80   cadence=85  hr=110
t=15    watts=140  cadence=88  hr=128
t=30    watts=200  cadence=90  hr=147
t=45    watts=260  cadence=92  hr=161
t=60    watts=260  cadence=0   hr=158   # stop pedalling, hold the last power reading
```

Values between samples are interpolated; `cadence=0` and gaps are meaningful and must
survive both players unchanged. The format is deliberately not JSON — these files get
read and hand-edited more often than they get parsed, and the parser is thirty lines
either way.

The property that makes this worth doing: **the same file, through both players, must
produce the same metrics** (within a stated tolerance). Layer A reaching a number the
frame parser cannot reach from the encoded frames means one of the two is lying, and the
round trip is what catches it.

## Layer A — the simulated bike

### Naming

`virtualbike` is taken. `src/virtualdevices/virtualbike.cpp` is the FTMS *server* QZ
advertises **towards** Zwift — the opposite direction from what is wanted here. Calling
the new thing "virtual bike" in code would collide with it in every grep for the rest of
the fork's life.

The device driver is **`simulatedbike`**, in `src/devices/simulatedbike/`. In prose,
"the virtual bike" is fine and is what this document is called; in code it is
`simulatedbike`, and `virtualbike` keeps its existing meaning.

### How it is selected

A new setting `simulated_bike` (default false), plus `-simulated-bike` on the command
line for the desktop build, matching the existing flag style in `main.cpp`.

Instantiation happens in the `bluetooth` constructor, *before* discovery starts: if the
setting is on, build a `simulatedbike`, wire `connectedAndDiscovered` and `debug` the way
the `ftmsbike` branch does at `bluetooth.cpp:855-861`, call `signalBluetoothDeviceConnected()`,
and return without creating a discovery agent at all. Deliberately **not** through the
old synthetic-`deviceDiscovered()` route: that route was deleted for good reasons and
faking an advertisement to reach a device that never advertised is how the fake-device
watchdog came to exist in the first place.

The dead toggles in `settings.qml` (Fake Device and its three siblings) are replaced by
one **Simulated bike** switch. Note the trap recorded in `2c39c5d`: removing keys from
`qzsettings` means renumbering `allsettingscount`, so the *keys* stay inert and only the
UI changes, unless the renumbering is done deliberately as its own change.

### What it must drive

`simulatedbike` inherits `bike` and does its work in a 200 ms timer, the way `fakebike`
did. What matters is what it must **not** reimplement: gears, ERG emulation,
resistance↔power conversion, `update_metrics()`, the peloton mapping and the metric
plumbing all live in `bike`/`bluetoothdevice` and must be reached through them. If the
simulated bike computes its own gear ratio, the gear code under test stops being the
shipped gear code and the whole exercise is theatre.

Driven from the scenario file, it must produce: instantaneous power, cadence, speed, heart
rate, resistance, distance, calories and crank revolutions, with `Distance` and `KCal`
accumulated from elapsed time so the odometer and calorie tiles move at a believable rate.

### Reacting to control

A player that only emits is half a bike. The simulated bike must also *respond*, because
the interesting loops are closed ones: a training program asks for 220 W, ERG converts it
to a resistance, and the tiles and the training app should both settle. So:

- `changePower(w)` — power converges on the request over a short lag with a small
  configurable noise band, rather than snapping to it. Snapping hides every oscillation
  bug ERG has.
- `changeResistance(r)` / `forceResistance(r)` — resistance follows the command, and
  power follows resistance × cadence through the same curve the scenario's
  `mode resistance` uses.
- Gear changes move the number the rider sees.
- `changeInclination()` from a simulated grade behaves like the trainer does.

Scenario `mode` decides which of these the file is asserting on; a `mode power` file
driving a power-controlled ride ignores an incoming resistance command in its assertions
but must still log it.

### What it proves, and what it does not

Proves: the app runs and the tiles populate with no bike present; gears, ERG, training
programs, the RTSS overlay, FIT writing and the DIRCON/FTMS output all have something to
chew on; a training app on the same network can be driven end to end from a desk.

Does not prove: anything about parsing a real frame, anything about the bytes QZ writes,
anything about the radio. That is Layer B and the deferred layer respectively, and the
distinction should be stated wherever the simulated bike is documented, because a green
tile on a simulated bike is very easy to mistake for evidence about the real one.

## Layer B — the frame harness

Goal: drive the **shipped** `ftmsbike` object with byte-exact frames, and assert on both
the metrics it derives and the bytes it writes back. In `tst/`, headless, deterministic,
in the existing gtest suite.

### The two seams

Two small, mechanical changes to `ftmsbike`. Both are necessary; neither is free, and the
hot path is involved, so they get reviewed as carefully as the feature.

**1. Inbound — a UUID-carrying entry point.**

Today the notification handler takes a `QLowEnergyCharacteristic` and calls
`.uuid()` on it. A test cannot fabricate one: `QLowEnergyCharacteristic` has no public
way to set its UUID — the data is filled in by `QLowEnergyServicePrivate` — so a
default-constructed instance reports a null UUID and every branch in the handler misses.
That single fact is why no test in this tree can pretend to be a 0x2AD2 notification.

Split the slot in two:

```cpp
// unchanged Qt slot: extracts the uuid and delegates
void ftmsbike::characteristicChanged(const QLowEnergyCharacteristic &c, const QByteArray &v) {
    handleNotification(c.uuid(), v, /*fromService=*/sender());
}

// all of the existing logic, addressable from a test
void ftmsbike::handleNotification(const QBluetoothUuid &uuid, const QByteArray &value, QObject *fromService);
```

Pure motion of code — no behaviour change, no reordering, and the `sender()` comparison
against `currentWriteService` that opens the handler moves in as an argument. `ftmsbike`
already calls itself with a default-constructed characteristic at `ftmsbike.cpp:820`, so
the pattern is not new to the file.

**2. Outbound — a write sink and a link gate.**

`processWriteQueue()` calls `request.service->writeCharacteristic(...)` directly
(`ftmsbike.cpp:168`) behind a check that the service state is `ServiceDiscovered`, and
`update()` returns early unless `m_control` exists and is in `DiscoveredState`
(`ftmsbike.cpp:679-700`). Both gates are correct in production and both make the object
inert in a test.

Two protected virtuals, whose default implementations are exactly today's code:

```cpp
virtual bool linkReady() const;                 // m_control && state()==DiscoveredState, service discovered
virtual void performWrite(const WriteRequest &); // service->writeCharacteristic(...)
```

The test subclass returns `true` and records. Nothing else changes; production takes the
same branches it takes now.

### The harness

`tst/Devices/simulatedftmsbike.h` — a test-only subclass:

```cpp
class simulatedFtmsBike : public ftmsbike {
  public:
    void notify(const QBluetoothUuid &uuid, const QByteArray &frame); // → handleNotification
    void tick(int ms);                                               // → update(), N times
    const QList<QByteArray> &writes() const;                         // what QZ sent to 0x2AD9
    void clearWrites();
};
```

Plus an encoder, `tst/Devices/ftmsframes.h`, that turns scenario samples into 0x2AD2
Indoor Bike Data frames with a chosen flag set — and is itself worth testing against the
worked examples in `docs/specs/FTMS_v1.0.pdf` and
`docs/specs/org.bluetooth.characteristic.indoor_bike_data.xml`, since an encoder bug and
a parser bug that agree with each other would cancel out and pass.

### Time

The honest hard part. `ftmsbike` reads `QDateTime::currentDateTime()` throughout —
distance accumulation, the ERG single-level hold (`ergSingleLevelHoldMs`), the Domyos
retry backoff, the calculated-resistance fallback. There is no clock seam and this spec
does **not** propose introducing one; threading an injected clock through that file is a
larger and riskier change than everything else here combined.

So Layer B tests are scoped to what survives without one:

- Assertions on values a single frame determines (speed, cadence, power, resistance,
  flag-dependent field offsets) are unaffected by the clock and are the bulk of the value.
- Accumulators (distance, calories) are asserted as monotonic and within a broad band, not
  to a figure.
- Anything whose *decision* depends on elapsed time keeps being tested the way this fork
  already tests it: extracted into a plain class with an injected clock, as the handshake
  and slew limiter already are. If a new time-dependent behaviour needs coverage, that is
  the route, not a clock seam in `ftmsbike`.

Stating this as a limit up front is deliberate. A harness that quietly sleeps to make
timing tests pass is worse than no timing tests.

### What it proves

Every branch of the frame parser reachable by a flag combination; the field-offset
arithmetic that flags shift around (the classic source of silent corruption, where a
missing `avgSpeed` field pushes cadence into the resistance slot); the bytes QZ writes to
the control point in response to a power, resistance or gear request; the routing
decisions in `ftmsCharacteristicChanged` that pass a training app's FTMS frames through
to the bike or refuse them.

## Scenarios

The first set, all as `.ride` files, all playable by both layers:

| Scenario | What it is for |
| --- | --- |
| `steady.ride` | 200 W / 90 rpm baseline. The one everything else is a deviation from. |
| `ramp.ride` | Soft-pedal to threshold over 60 s. Tile update rate, chart, ERG tracking. |
| `sprint.ride` | 900 W for 8 s. Peak-power tiles, FIT max values, chart scaling. |
| `coast.ride` | Cadence to zero mid-ride. The zero-cadence path, `lastGoodCadence`, and the tiles that must not go stale or NaN. |
| `dropout.ride` | Frames stop for 10 s, then resume. What the tiles do while nothing arrives. |
| `erg-hold.ride` | Training program asks for a target; the bike converges. The closed loop. |
| `erg-single-level.ride` | Repeated ±1 level requests — the `ergSingleLevelHoldMs` behaviour. |
| `gear-shift.ride` | Shift through the cassette under load. Gear table rows → resistance, neutral gear. |
| `resistance-lvl.ride` | A bike that reports native resistance rather than power. |
| `handshake-refused.ride` | Control point answers `CONTROL_NOT_PERMITTED`. Layer B only. |

Layer B additionally gets frame-level cases with no scenario behind them: truncated
frames, unknown flag bits set, a 0x2AD2 that is one byte short of the length its flags
imply (`ftmsbike.cpp:1145` already guards this — the test proves the guard).

## Fixtures: where the bytes come from

Both sources, and they answer different questions.

**Synthetic** — the `.ride` files above, written by hand. Readable, deterministic, and
the only way to write a test whose intent is obvious a year later. Their weakness is
structural: they can only contain frames someone already believed in.

**Recorded** — `tools/qzlog2ride.py`, a small script that reads a QZ `debug-*.log` and
extracts the `<<` and `>>` hex lines with their timestamps and UUIDs into a fixture the
harness can replay verbatim. This is where the bike's actual quirks live — the flag
combination it really sets, the fields it really omits, what it really answers on the
control point — and none of that is inventable at a desk.

The workflow that follows: ride the bike once with logging on, keep the log, and every
future change is regression-tested against that recording without the bike present. A
bug found on the trainer becomes a fixture the same day it is found.

btsnoop stays a fallback for the case where a capture predates QZ's own log, and
`btlogs/` is explicitly not a source — those are upstream's Domyos captures.

## CI

Layer B lands in `tst/qdomyos-zwift-tests.pro` and runs in `linux-x86-build` with
everything else. No new job, no new dependency.

Layer A gets a headless smoke run in the same job: build with the simulated bike enabled,
play `steady.ride` for a fixed number of seconds, and assert the final metrics against the
scenario. This needs a small addition to `main.cpp` alongside the existing `-smoke-test`
flag — something of the shape `-simulated-bike -ride <file> -ride-report <json>` — that
runs the device without QML and writes the resulting metrics out. It exercises the device
and the metric plumbing, not the tiles.

**Tile assertions are deliberately out of scope for now.** `homeform` cannot be
constructed without a loaded QML engine — `homeform.cpp:974` reaches straight for
`engine->rootObjects().constFirst()` — so there is no headless `homeform` to query, and
the cheapest route to one is running the real QML under `QT_QPA_PLATFORM=offscreen` (the
job already has Xvfb) rather than refactoring `homeform`. Worth doing eventually; not
worth blocking this on. Until then the tiles are verified by looking at them, which is
what Layer A is for.

## Phases and acceptance

**Phase 0 — the scenario format.** Parser and the first three `.ride` files, in `tst/`.
No product code touched. *Accepted when* the parser round-trips every file and the suite
still passes.

**Phase 1 — Layer A, the simulated bike.** `src/devices/simulatedbike/`, the setting, the
command-line flag, the `bluetooth` constructor hook, the settings.qml switch. *Accepted
when* QZ starts on Windows with no bike and no radio, the tiles move through
`steady.ride`, gears and ERG respond, a training app on the network sees a bike over
DIRCON, and a FIT file is written at the end. Low risk: it adds a device, it does not
change `ftmsbike`.

**Phase 2 — Layer B, the harness.** The two seams, `simulatedFtmsBike`, the frame encoder,
and the parse tests. *Accepted when* the seams are provably behaviour-neutral (the
existing suite passes unchanged, and a real ride against the bike behaves identically),
the encoder matches the spec's worked examples, and every scenario in the table has a
test asserting metrics and — where applicable — written bytes.

**Phase 3 — recording.** `tools/qzlog2ride.py`, one captured ride from the real bike
committed as a fixture, replayed in CI. *Accepted when* a log from a ride replays to the
same metrics the ride produced.

**Phase 4 — CI smoke for Layer A.** The `-ride`/`-ride-report` flags and the job step.
*Accepted when* it fails if a metric drifts.

Phases 1 and 2 are independent after Phase 0 and can be done in either order. Phase 1
first is recommended: it is a day's work, it touches nothing dangerous, and it makes every
subsequent change to the UI verifiable at a desk — which is the thing that is impossible
today.

## Deferred: a real peripheral

Nothing above touches a radio. The layer that would — a BLE peripheral advertising the
FTMS service that QZ discovers and connects to for real — is the only way to test
discovery, the WinRT backend, bonding, unpaired connection and reconnect backoff, which
are precisely the areas this fork changed most. It is deferred because it needs a second
radio or a second machine, cannot run in CI, and is nondeterministic in the way radios
are.

When it is picked up, the cheapest starting point is already in the tree: `QZ_ESP32/`
holds an Arduino sketch for an ESP32 FTMS peripheral. A `.ride` file playing through an
ESP32 would make it the third player of the same scenario format, which is an argument for
getting that format right now rather than later.

## Risks and open questions

- **The seams are in the hot path.** `characteristicChanged` and `processWriteQueue()` are
  what every ride runs through. The split is mechanical and behaviour-neutral by
  construction, but "by construction" is a claim, not evidence: Phase 2 is not done until
  a real ride against the bike has confirmed it.
- **The encoder and parser can agree on the same mistake.** Mitigated by testing the
  encoder against the spec PDFs independently, and by the recorded fixtures of Phase 3,
  which no encoder of ours produced.
- **Layer A's realism is a judgement call.** How fast simulated power converges on an ERG
  request decides whether ERG oscillation bugs are visible or hidden. The lag and noise
  need to be configurable in the scenario file, and the defaults need to come from a real
  ride's log rather than from taste.
- **The clock.** Stated above as a scoping limit. If a future bug is only reachable
  through elapsed time inside `ftmsbike`, this design will not reach it, and the answer is
  to extract the decision — not to retrofit a clock into the harness.
- **Open: does the simulated bike advertise itself as a specific model?** Several
  behaviours in `ftmsbike` are gated on device-name flags (`DOMYOS`, `FS_YK`, `D500V2` and
  some fifty others). Layer B can set them by connecting through the real detection path
  with a chosen name, which makes those branches testable — but only if the scenario file
  can name the bike it is pretending to be. Cheap to add, and probably belongs in the
  format from the start.
