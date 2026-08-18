# The virtual bike

**Phases 0 and 1 are implemented. The rest is spec.**

## The endgoal

**CI runs a fake bike on a fake trainer app and asserts.**

One process pair on one Linux runner, no radio and no hardware: a simulated bike playing a
ride scenario at one end, a fake training app discovering and consuming QZ at the other,
and assertions in between. When that is green, a change to QZ's metrics, gears, ERG or
DIRCON output is checked before anyone gets on a trainer.

Everything below is a step towards that sentence, and any piece of this plan that does not
serve it is negotiable.

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

## Shape: one scenario, both ends of QZ

The spine of the design is that a **ride scenario** is one file, and everything else is
either a way of playing it into QZ or a way of watching what comes out:

```
                       tst/fixtures/rides/*.ride
                                  |
              +-------------------+-------------------+
              |         the bike end, either way      |
              |                                       |
         Layer A                                  Layer B
      simulatedbike                     FTMS frames -> real ftmsbike
   drives metrics directly           the shipped parser, fed from the
                                     same file or from a recorded ride
              |                                       |
              +-------------------+-------------------+
                                  |
                   bike / bluetoothdevice: metrics, gears, ERG
                                  |
                    virtualbike + DIRCON + mDNS  (QZ's output)
                                  |
                               Layer C
                    fake training app: discover, connect,
                    read the stream, write control commands
                                  |
                               ASSERT
```

Two bike ends, one output side, one fake consumer. The bike ends are interchangeable
because they are fed by the same file, which is what lets the same assertions run against
the simulated bike *and* against the real `ftmsbike`.

A scenario is a plain, readable, timestamped list of what the rider is doing:

```
# ramp.ride — 60s ramp from soft-pedal to threshold, steady cadence
mode        power            # what the file asserts on: power | resistance | speed
bike        YPBM1234         # optional: the device name to pretend to be
resistance  12               # optional: starting resistance level
erg_lag     2.0              # optional: seconds for simulated power to reach an ERG target
noise       0.03             # optional: ± fraction of noise on simulated power

t=0     watts=80   cadence=85  hr=110
t=15    watts=140  cadence=88  hr=128
t=30    watts=200  cadence=90  hr=147
t=45    watts=260  cadence=92  hr=161  silence=10   # the bike goes quiet for ten seconds
```

Two rules settle what a file means, and both were chosen to be the ones hardest for two
players to disagree about.

**Each field is its own timeline.** A value interpolates between the two nearest samples
that both state it, is absent before the first that states it, and holds after the last.
So `hr` can appear in one sample in ten without dragging its neighbours to zero, and
`cadence=0` is an explicit value that must be reproduced exactly rather than read as "said
nothing". Absence and zero are different things and the format keeps them different.

**Interpolation is always linear.** No step mode, no easing, no per-field override. A fast
transition is written by putting two samples close together, which is also what the
recorded fixtures look like. Every extra mode would be one more way for the two players to
disagree about what the file meant.

`silence=<seconds>` is the exception that is not about values: the bike reports nothing at
all for that long — not stale numbers, nothing — which is the only way to write a dropout.

The format is deliberately not JSON: these files get read and hand-edited more often than
they get parsed, and the parser is thirty lines either way.

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

A **Simulated bike** switch goes into settings.qml next to the inert Fake Device one, with
a path field for the scenario. The dead toggles are deliberately *left alone* rather than
removed: `src/CLAUDE.md` requires new settings to be declared last and `allSettingsCount`
to be kept in step, and pulling four keys out of the middle of that list is a renumbering
with its own risk and no relation to this feature. They stay inert and misleading until
someone removes them on purpose. The stale line in `docs/android-testing/README.md` is in
the same position.

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

## Layer C — the fake training app

The half of QZ that this fork actually rewrote is the half pointing at Zwift. Read the
DIRCON and mDNS section of FORK.md: the endpoint had no process lifetime, announcements
went out on one interface, loopback queries went unanswered, the service type was not
fully qualified, QZ renamed its own service every five seconds by mistaking its own
announcement for a competing claim, the SRV target carried a smuggled space, and no
goodbye was sent on quit. Every one of those was found by a real training app refusing to
connect, and not one of them is testable today without launching Rouvy or MyWhoosh on a
second machine — MyWhoosh cannot even share a host with QZ.

A fake training app is what closes that gap, and it is cheap because DIRCON is small and
entirely described in this tree: a six-byte header, seven message ids and eight response
codes in a 70-line `dirconpacket.h`, a 255-line parser, and the server in
`dirconprocessor.cpp`. The client is: browse mDNS, open a TCP socket, `DISCOVER_SERVICES`,
`DISCOVER_CHARACTERISTICS`, `ENABLE_CHARACTERISTIC_NOTIFICATIONS` on 0x2AD2, read the
stream — and, when it wants to steer, `WRITE_CHARACTERISTIC` to 0x2AD9 with a target power
or a set of simulation parameters.

### In-process, not two processes

The fake app is a gtest in the existing suite, talking over a real TCP socket to a real
`DirconManager` built on a real `simulatedbike`, all inside the test binary. Everything is
already linkable: the tests link the whole static library, `DirconManager` takes a
`bluetoothdevice *`, and `simulatedbike` is in the library as of Phase 1.

Two things make this practical rather than theoretical, both observed today rather than
assumed. QZ runs headless — `-no-gui` rode a scenario end to end with no display and no
radio. And the BLE half of the virtual device fails politely on a machine with no adapter:
it logs "virtual bike bluetooth not connected" and the DIRCON endpoint comes up anyway,
serving the full FTMS, cycling-power and cadence service set. `virtual_device_bluetooth`
turns the BLE half off explicitly, which is what the test should do rather than relying on
the failure being graceful.

A second, coarser check runs the shipped binary — `qdomyos-zwift -no-gui -simulated-bike
-ride steady.ride` — with the fake app against it as a separate process. That one proves
the real executable serves what the in-process test says it serves. It is a smoke test,
not the primary assertion, because process orchestration in CI is where flakiness comes
from.

### What it asserts

- **Discovery.** A service record is answerable, including over loopback; the advertised
  name is unchanged thirty seconds later; the SRV target is a legal hostname; a goodbye
  goes out on quit.
- **Lifetime.** The endpoint answers before any bike exists and outlives one — the change
  that gave DIRCON process lifetime, currently protected by nothing.
- **Enumeration.** The service and characteristic set is the expected one.
- **The stream.** Notifications on 0x2AD2 decode to the numbers the `.ride` file states.
  This is the end-to-end assertion the whole plan is for: scenario → bike → metrics →
  virtual device → wire.
- **Control.** A written target power reaches the bike and changes what comes back.

### Decode with literals, not with our own encoder

The client must not parse QZ's output by handing it to `DirconPacket`. That would be the
"encoder and parser agreeing on the same mistake" risk one level up: a framing error in
`DirconPacket` would be invisible because both sides share it. For the handful of packets
that matter, the expected bytes are written out as literals in the test, checked once
against the DIRCON description and the FTMS spec in `docs/specs/`. The same argument
applies to discovery, where an independent mDNS implementation — python's `zeroconf` in
the two-process check — is worth more than reusing the in-tree `qmdnsengine` that QZ
announces with.

### What it still cannot tell you

It tests QZ's conformance to DIRCON *as this codebase understands it*. It cannot discover
that MyWhoosh refuses a device advertising the same IP as the host it runs on, because
nothing here knows that rule — it was learned from MyWhoosh. Layer C catches regressions
and protocol errors. The real apps' idiosyncrasies are still learned the hard way, and
still belong in FORK.md when they are.

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

Everything that asserts lands in `tst/qdomyos-zwift-tests.pro` and runs in
`linux-x86-build` with everything else. No new job and no new dependency: the loop is a
gtest talking to a TCP socket on localhost, which the runner already provides. The
two-process smoke test is the only piece that needs orchestration, and it is deliberately
the least load-bearing one.

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

**Phase 0 — the scenario format. Done.** `RideScenario` in
`src/devices/simulatedbike/ridescenario.{h,cpp}` — Qt-free and C++11, so both players can
link it and neither needs a QObject to read a file. Five fixtures in `tst/fixtures/rides/`
(`steady`, `ramp`, `sprint`, `coast`, `dropout`); the remaining five in the scenario table
arrive with the phases that assert on them. 14 tests in `tst/Devices/TestRideScenario.h`,
including the round trip this phase was accepted on and nineteen malformed files that must
be rejected with a reason. It went into `src/` rather than `tst/` as first sketched,
because Layer A needs it too and one copy is the point.

**Phase 1 — Layer A, the simulated bike. Done, with one part unverified here.**
`simulatedbike` in `src/devices/simulatedbike/`, the `simulated_bike` and
`simulated_bike_ride` settings, `-simulated-bike` and `-ride <file>` on the command line,
the hook in the `bluetooth` constructor, and a Simulated bike switch in settings.qml. It
computes no gears, no ERG conversion and no metric bookkeeping of its own — all of that is
reached through `bike`/`bluetoothdevice`, which is the only thing that keeps the exercised
code the shipped code.

Verified on a Linux build: the app starts with no radio, skips discovery, loads the
scenario, and rides it — `ramp.ride` produces the cadence and power the file states, the
starting resistance comes from the file, and the virtual bike and DIRCON endpoint come up
and serve. The QML app would not start in that build container (it exits the same way with
no device at all, so it is the environment and not the device), so the part of the
acceptance criteria that needs a screen was left to be checked by eye elsewhere.

**Verified on Windows, 2026-08-17, and it paid for itself immediately.** The app runs and
the tiles populate against `-simulated-bike -ride <file>` with no trainer in the room, and
the four DIRCON bugs that had kept MyWhoosh from ever working — `daa73a305`, `6849b694f`,
`343e42b90`, `f8af0cb21`, see FORK.md — were found, fixed and confirmed across four
build-test rounds **with the simulated bike as the bike end every time**. Rouvy and
MyWhoosh both discovered QZ, subscribed, and received power that no trainer produced. That
is the Layer A claim above — *a training app on the same network can be driven end to end
from a desk* — measured rather than asserted, and the strongest single argument this
document has for its own existence.

It also draws the limit sharply. Those constants are exactly why the trainer still has to
confirm the ride: the simulated bike proved the wire and the client's rules, and it proved
nothing about `ftmsbike`'s parse, which is Phase 6's job.

**Verified on Android, 2026-08-17**, on the API 34 x86_64 emulator, built from
`lite-version` on the build VM. Android has no command line, so the route is the settings
pair: `simulated_bike=true` and `simulated_bike_ride` pointing at a `.ride` file. Nothing
ships the fixtures to the device, so the file has to be put there — the app's own private
directory works and needs no storage permission, and `RideScenario::load` is a plain
`std::ifstream`, so any readable path will do.

The full recipe — pushing the file, editing the INI at
`files/.config/Roberto Viola/qDomyos-Zwift.conf`, and what to check afterwards — is in
`docs/android-testing/README.md`, kept there because that is where anyone looking for an
Android procedure will start.

What it produced, playing `ramp.ride`: **"Simulated Bike found"**, cadence 92 with an
average of 89 (the file ramps 85 → 92, so the average is the interpolation working rather
than a step), 260 W with an average of 204, resistance 10 from the file's `resistance`
directive, and an odometer accumulating. `simulatedbike playing …/ramp.ride duration 90 s`
in the log. The DIRCON endpoint came up on 36866 and the HRM one on 36867. Set
`log_debug=true` in the INI first — without it QZ writes no log on Android and there is
nothing to read.

**The BLE virtual bike advertises here too — after the device is renamed.** The first
attempt failed, and the failure is worth keeping because it looks like a QZ bug and is not
one:

```
QtBluetoothGattServer: Starting to advertise.
QtBluetoothGattServer: Advertising failure: 1
virtualbike::controller:ERROR AdvertisingError
virtual bike bluetooth not connected
```

Android's `AdvertiseCallback` code 1 is `ADVERTISE_FAILED_DATA_TOO_LARGE`. The cause is the
**adapter's own name**: a BLE advertisement is 31 bytes, and Qt's Android backend includes
the device name rather than the `setLocalName("QZ")` that `virtualbike.cpp:89` asks for.
The emulator ships as `sdk_gphone64_x86_64` — 19 characters, 21 bytes with its header —
and with flags, TX power and the service UUID there is no room left. QZ already knows this:
`homeform.cpp:1086` toasts *"Bluetooth name too long, change it to a 4 letters one in the
android settings"* whenever the name exceeds nine characters or leaves `[A-Za-z0-9 ]`.

Renaming the device to `QZ1` produced, on the next launch:

```
QtBluetoothGattServer: Services successfully added: true
BtGatt.AdvertiseManager: onAdvertisingSetStarted() - regId=-2, advertiserId=0, status=0
virtualbike::bikeProvider "virtual bike connected"
virtualbike::writeCharacteristic Unknown Service  64 02 4c 06 ab 00 0a 00 60 00 00 00
```

— Indoor Bike Data with flags `0x0264`, once a second, carrying the scenario's numbers,
plus the Heart Rate writes behind it. **So the emulator covers the BLE peripheral half as
well**, which is the half Windows can never cover, and the Android leg of the test stage is
therefore complete rather than partial.

Two things about the rename, because both cost time. `adb shell settings put secure
bluetooth_name` **does not stick** — the Bluetooth stack rewrites it from the product model
every time the adapter is enabled. It has to go through Settings → About → Device name,
*including* the second confirmation dialog, which is what actually commits it. And cycling
the adapter with `svc bluetooth` or `cmd bluetooth_manager` killed the emulator outright
twice; renaming needs no adapter cycle, so do not add one.

This also lands on the tablet: any Android host meant to serve Kinomap over BLE needs a
short device name, or the virtual bike will never advertise and nothing will say why except
a toast that is easy to dismiss.

**DIRCON on the emulator is reachable from Windows**, which matters more than it sounds:

```bash
adb forward tcp:37866 tcp:36866   # then connect to 127.0.0.1:37866
```

Connecting that way immediately yields real Indoor Bike Data — `…2ad2…` with flags
`0x0264`, power present — and a `0x2a63` frame behind it. So Layer C can be pointed at the
emulator over a forwarded port, with the caveat that mDNS discovery cannot cross the
emulator's NAT: a test client there must be given the address rather than find it.

Built on the VM, run on the emulator — `C:\VMs\qz-build\README.md`, and prefer
`rebuild-and-test-emu.sh`, which builds x86_64 alone in roughly a quarter of the time.

**Phase 2 — Layer C, connect and enumerate.** A DIRCON client in the test project,
in-process against a `simulatedbike`, with the BLE half of the virtual device switched off.
*Accepted when* it connects, enumerates the service and characteristic set, and asserts it
against literals rather than against our own encoder. This is the first phase of the
endgoal proper and the one that proves the shape works.

**Phase 3 — the asserting loop. This is the endgoal.** Notifications on 0x2AD2 decoded and
asserted against the `.ride` file the bike is playing, then a written target power asserted
to change what comes back. *Accepted when* `steady.ride` produces the power and cadence it
states, `coast.ride` drives them to zero, `dropout.ride` produces a real gap rather than
stale numbers, and an ERG request moves the stream — all in `linux-x86-build`, with no
hardware and no training app. When this is green the sentence at the top of this document
is true.

**Phase 4 — discovery.** The mDNS half, with an independent implementation, covering the
fixes FORK.md lists: every interface, loopback, fully-qualified type, a name that stays
put, a legal SRV target, a goodbye on quit. Plus the two-process smoke test against the
shipped binary. *Accepted when* each fix in that list has a test that fails if the fix is
reverted.

**Phase 5 — recording.** `tools/qzlog2ride.py`, turning a debug log into both halves: the
`<<` frames become a bike, and the `>>` frames become an oracle for what QZ wrote in
response. *Accepted when* a log from a real ride replays to the metrics that ride produced,
and the oracle flags a deliberate change to what QZ sends.

**Phase 6 — Layer B, the frame harness.** The two seams in `ftmsbike`, the harness, the
frame encoder, and the parse tests — now with a clearer purpose than when this document
was first written: it is the *second bike end* of the same loop, so that everything Phases
2–4 assert can be re-run with the real driver in place of the simulated one, fed from a
scenario or from a recorded ride. *Accepted when* the seams are provably behaviour-neutral
(the suite passes unchanged and a real ride behaves identically), and the Phase 3
assertions pass with `ftmsbike` as the bike end.

### Why Layer B moved

The original plan put the frame harness second, on the reasoning that it catches the most
protocol bugs. The endgoal reorders it, and the reasoning is worth writing down rather
than quietly acting on:

- Layer B tests **parsing inherited from upstream**, which largely works and changes
  rarely. Layer C tests **the output side this fork rewrote**, which has had at least
  seven distinct bugs, every one found by a real app refusing to connect.
- Layer B needs two seams in `ftmsbike`'s hot path. Layer C needs no product change at
  all — every piece it touches is already constructible from a test.
- Layer C is what makes the endgoal sentence true. Layer B, on its own, never can be:
  it has no consumer at the other end.

Layer B does not shrink in importance, it changes role. As a component of the loop it is
worth more than it was as a parallel track, because its assertions become the same
assertions rather than a second set.

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

## What the endgoal still does not cover

Worth being exact, because "CI runs the loop and asserts" is easy to hear as "CI tests QZ".

- **The radio.** Discovery over BLE, the WinRT backend, bonding, unpaired connection and
  reconnect backoff are untouched by any of this. Deferred below.
- **The real training apps.** See the end of Layer C: their undocumented rules are not
  knowable from here.
- **The tiles.** `homeform` cannot be constructed without a loaded QML engine
  (`homeform.cpp:974`), so the loop asserts on the metrics and the wire, not on what is
  drawn. The cheapest route to that is running the QML under `QT_QPA_PLATFORM=offscreen`
  and querying homeform's properties; the runner already has Xvfb. Worth doing once the
  loop is green, and not before.
- **`ftmsbike`, until Phase 6.** With `simulatedbike` as the bike end, the real device
  driver is not in the loop at all. This is the sharpest limit of the endgoal as stated:
  a green loop says nothing about the code that talks to your trainer until the second
  bike end exists.

## Risks and open questions

- **A fake client can agree with a broken server.** Mitigated by decoding with literals
  and by using an independent mDNS implementation — see Layer C. It is the same structural
  risk as the encoder/parser one below, and it is the reason neither is allowed to be
  checked only against itself.
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
- **Settled: the scenario can name the bike it is pretending to be.** Several behaviours in
  `ftmsbike` are gated on device-name flags (`DOMYOS`, `FS_YK`, `D500V2` and some fifty
  others), and Layer B will only reach those branches if it can connect through the real
  detection path under a chosen name. The `bike` directive is in the format and parsed from
  Phase 0; nothing reads it yet, and Layer B is what will.
