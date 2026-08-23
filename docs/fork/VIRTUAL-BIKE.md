# The virtual bike

**All six phases are implemented — the endgoal at the top of this document is met, and both
bike ends now feed the same loop.**

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

**The "Fake Device" toggle did nothing, and no longer exists.** `settings.qml` offered
it and `applewatch_fakedevice` still round-trips to `QSettings`, but no code path in
this tree ever instantiated a device from it; its only effect was suppressing the
first-run wizard. Phase 7c-2b deleted the screen, the wizard and the class that read
it, so the key survives with nothing reading or writing it — one for phase 6. The treadmill,
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

**Tile assertions were deliberately out of scope, and the question has since gone away.**
`homeform` could not be constructed without a loaded QML engine, so there was no headless
object to query and the tiles were verified by looking at them, which is what Layer A is
for. Phase 7c-2b deleted the tiles altogether. What replaced them is `RideState`, which
is a plain QObject that constructs with a null bridge and is asserted directly by
`tst/UI/TestRideState.cpp` — no QML engine, no offscreen platform, no Xvfb.

## Phases and acceptance

**Phase 0 — the scenario format. Done.** `RideScenario` in
`src/devices/simulatedbike/ridescenario.{h,cpp}` — Qt-free and C++11, so both players can
link it and neither needs a QObject to read a file. Five fixtures in `tst/fixtures/rides/`
(`steady`, `ramp`, `sprint`, `coast`, `dropout`), joined by `erg-hold` in phase 3; the
remaining four in the scenario table arrive with the phases that assert on them. 14 tests in `tst/Devices/TestRideScenario.h`,
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
and with flags, TX power and the service UUID there is no room left. QZ used to say so:
`homeform.cpp` toasted *"Bluetooth name too long, change it to a 4 letters one in the
android settings"* whenever the name exceeded nine characters or left `[A-Za-z0-9 ]`.
**That warning went with the class in phase 7c-2b and has no replacement**, so the
symptom now arrives with no explanation attached. Worth re-posting through `QzNotify`.

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

**Phase 2 — Layer C, connect and enumerate. Done.** `DirconFakeApp` in
`tst/Devices/TestDirconFakeApp.h` — fifteen tests, a real TCP client against a real
`DirconManager` on a real `simulatedbike`, all inside the test binary and all in
`linux-x86-build` with everything else. The endpoint is built by calling
`DirconManager::shared()` directly rather than through `virtualbike`, so the BLE half is
not switched off, it is never constructed: nothing here can fail on a machine with no
adapter because nothing here asks for one.

What it covers: the service set on both endpoints (0x1826 / 0x1818 / 0x1816 on
`base + DM_MACHINE_WAHOO_KICKR`, 0x180D on `base + DM_MACHINE_WAHOO_BLUEHR`), the
characteristic set and property flags of each, the readable values — 0x2ACC, 0x2AD6,
0x2AD3, 0x2A65, 0x2A5D, 0x2A5C — the four refusals (service not found, characteristic not
found, reading a notify-only characteristic, and the undocumented message 0x07 that has to
be answered anyway), the notification subscription acknowledgement, and the Rouvy profile's
single folded endpoint.

Two of those are worth naming because nothing else protects them. **0x2ACC** is asserted
byte for byte, which is the FTMS feature word MyWhoosh reads to decide what the trainer can
report — `f8af0cb21` in FORK.md. And the **process lifetime** of the endpoint gets two
tests: it answers with no device attached at all, and a client already connected keeps
working across a bind, a rebind and the bike going away again. That is the change the whole
DIRCON refactor rests on and it had no test until now.

*Accepted on*: the expected bytes are not our encoder's output. Every literal was captured
from the shipped `qdomyos-zwift -no-gui -simulated-bike -ride steady.ride` binary with an
independent python client that shares nothing with `DirconPacket`, in both the default and
the `rouvy_compatibility` profiles, and the request sequence the tests send is the one
Rouvy actually sends — read off a real Rouvy session in a `debug-*.log`, discover services,
discover 0x1826 and 0x1816, read 0x2ACC / 0x2AD6 / 0x2A5C, subscribe to 0x2AD2 and 0x2A5B,
then write 0x2AD9.

One thing the capture settled that the spec had not: **notifications arrive whether or not
anyone subscribed** unless `wahoo_rgt_dircon` is on, so the client has to split frames off a
stream and set unsolicited ones aside rather than assume the next frame answers the last
request. Phase 3 inherits that machinery — the same run showed `steady.ride` arriving on
0x2AD2 as `6402730cb4000c00c8008400`: flags 0x0264, 31.87 km/h, 90 rpm, resistance 12,
200 W, which is the file, on the wire, with no trainer in the room.

**Phase 3 — the asserting loop. Done. This was the endgoal.** `DirconRideLoop` in
`tst/Devices/TestDirconRideLoop.h` — six tests, the same in-process client as phase 2
(extracted to `tst/Devices/DirconTestClient.h`), now subscribed to 0x2AD2 and asserting on
what arrives. **The sentence at the top of this document is true**: a `.ride` file plays
into `simulatedbike`, through `bike`/`bluetoothdevice`'s metrics, through
`CharacteristicNotifier2AD2`, out over DIRCON, into a fake training app that checks the
numbers — in `linux-x86-build`, with no radio, no trainer and no training app.

Each acceptance criterion, and what it turned into:

- **`steady.ride` produces the power and cadence it states.** Every frame: flags 0x0264,
  200 W, 90 rpm, resistance 12 from the file's directive, speed non-zero. Exact, not a
  range — the file is flat and `noise` defaults to zero.
- **`coast.ride` drives them to zero.** Cadence, power and speed all exactly zero through
  the standstill, *while heart rate keeps falling*. That second half is the assertion that
  matters: it proves the bike is still talking and reporting zeroes, which is the whole
  distinction between a coast and a dropout.
- **`dropout.ride` produces a real gap rather than stale numbers.** **This one was wrong,
  and the fix is the wording rather than the code.** QZ cannot produce a gap:
  `DirconManager::bikeProvider()` notifies on its own timer and sends whatever the metrics
  currently hold, so a silent bike is a frame that repeats. Measured against the shipped
  binary before the test was written — eleven seconds of one byte-identical frame, then a
  step to the far side of the gap. So the test asserts *that*: byte-identical frames
  throughout, heart rate included, then a jump to the post-gap sample with no value from
  the middle ever appearing. Stale numbers are what a dropout looks like on this wire; what
  had to be proved is that nothing is *invented* while the bike says nothing.
- **An ERG request moves the stream.** `REQUEST_CONTROL` and `SET_TARGET_POWER` are
  acknowledged with their exact FTMS replies (`80 00 01` and `80 05 01`, carried inside the
  WRITE_CHARACTERISTIC response), and a 300 W target against `erg-hold.ride`'s flat 160 W
  pulls the stream to within 20 W of the target and moves the resistance with it. The new
  `erg-hold.ride` fixture is the scenario table's, with `erg_lag 1.0` so a test does not
  wait on a flywheel.

Plus one the plan did not ask for: a write with **no bike attached** is refused rather than
dereferencing a freed device. That is the guard the process-lifetime endpoint needs, and
the refusal is silence — `processPacket` sets `DPKT_MSGID_ERROR` and nothing is sent — so
the test asserts that nothing comes back.

**These tests take about seventy-five seconds**, and that is the honest cost of the design
rather than something to optimise away. `simulatedbike` advances its ride by measured
wall-clock time on purpose, so a test that needs `t=21` waits twenty-one seconds. `coast`
and `dropout` are therefore one test each, paying the wait once. A clock seam would remove
the wait and remove the thing being tested with it; if this ever becomes intolerable the
answer is a shorter fixture, not a fake clock.

**It found a real bug, which is the point.** The longer-lived endpoints reached a path the
250-millisecond phase 2 tests never did, and `DirconProcessor`'s destructor segfaulted:
the mDNS server, hostname and provider are all children of the processor and are added in
that order, so QObject freed the server first and `~ProviderPrivate()` sent its goodbye
through it afterwards. Every mid-session teardown — `releaseShared()`, which
`DirconManager::shared()` calls when a treadmill replaces a bike — was a use-after-free,
and the goodbye that path exists to send was going into freed memory rather than onto the
wire. Fixed by giving the destructor an explicit dependency order; see step 4 of
`DIRCON-SERVER-REFACTOR.md`, whose claim that "nothing else is required here" this
disproves.

**Phase 4 — discovery. Done.** `DirconDiscovery` in `tst/Devices/TestDirconDiscovery.h` —
eight tests over a DNS codec written from RFC 1035 and RFC 6762 in
`tst/Devices/MdnsTestClient.h`, sharing nothing with the `qmdnsengine` QZ announces with.
That independence matters more here than anywhere else in Layer C: a browser built on
`qmdnsengine` would only be asking that code whether it agrees with itself.

*Accepted when* each fix has a test that fails if the fix is reverted — so each fix **was**
reverted, rebuilt and re-run:

| Fix | Test | Reverted → |
| --- | --- | --- |
| Fully-qualified service type | `TheServiceAnswersAQueryForItsFullyQualifiedType` | fails |
| A legal SRV target | `TheSrvTargetIsALegalHostnameAndPointsAtTheListeningPort` | fails |
| A name that stays put | `TheServiceNameSurvivesItsOwnRecordComingBack` | fails |
| A goodbye on quit | `AGoodbyeIsSentWhenTheEndpointGoesAway` | fails |
| Every interface | `AnnouncementsGoOutOnEveryInterface` | fails |
| Loopback answered | `ABrowserQueryIsAnsweredOnLoopback` | fails |

Two of those needed more than a first draft, and both are worth knowing:

- **Two of the fixes live in two places.** Reverting `setType("…_tcp.local.")` alone changes
  nothing, because `Provider::update()` appends the trailing dot itself; and reverting
  `serverName.replace(' ', '-')` alone changes nothing, because
  `HostnamePrivate::assertHostname()` rewrites every character outside `[A-Za-z0-9-]` to a
  hyphen. Only reverting both ends made either test fail. The tests assert the observable
  contract rather than one of the two implementations, which is the right thing for them to
  assert — but "one fix, one place" was an assumption, and it was wrong.
- **The rename loop is provoked, not waited for.** Watching a name for twelve seconds proved
  nothing: with the guard removed, the service still settled. The self-conflict needs a
  *response* carrying the proposed record, and in the current architecture nothing produces
  one during the probe window. So the test sends it — a hand-built SRV response that is
  byte-for-byte the provider's own claim, repeated through the probe. RFC 6762 8.2 says an
  identical record is not a conflict; a responder that renames on this is renaming against
  itself, which is exactly the bug.

**Two tests can skip, and on the CI runner they probably will.** `ABrowserQueryIsAnswered`
`OnLoopback` joins the group on the loopback interface *only* — a listener joined everywhere
would be satisfied by an answer that went out the Wi-Fi adapter, which is the failure being
tested for — and Linux does not set `IFF_MULTICAST` on `lo`, so there is no loopback
multicast to assert on. `AnnouncementsGoOutOnEveryInterface` needs two multicast-capable
interfaces before "sent to one" and "sent to all" are distinguishable at all. Both skip with
the reason printed rather than passing quietly. They are real tests on a multi-homed
developer machine — which is where both bugs appeared — and no-ops on a single-homed VM.
That is a genuine gap against "anything meant to catch a regression has to run in CI", and
naming it is better than pretending the coverage is there.

Three things the wiring taught, none of them guessable:

- **A unicast query to 127.0.0.1:5353 does not reach QZ.** It is delivered to exactly one of
  the sockets sharing that port, and QZ is never alone there — Bonjour's `mDNSResponder`,
  Windows' `Dnscache` and `adb` were all holding 5353 on the machine this was written on. So
  the browser asks on the group, which every joined socket receives, and is answered
  *unicast* back to its ephemeral port, because `Message::reply()` only answers on the group
  when the query came from port 5353. Ask multicast, listen unicast.
- **The tests run in the Rouvy profile**, which builds one endpoint and one provider. The
  default profile builds two and both answer, so every assertion would have to sort two
  interleaved replies — test machinery with no product behind it.
- **`dircon_id` is set to 4321 for the same reason the port is 47820.** The Rouvy profile
  turns the default id of 0 into 1234, so a developer with QZ running announces the name the
  tests were claiming. Two responders claiming one name is a real conflict and the loser
  renames itself, which would fail the suite for a reason that has nothing to do with the
  code.

**The two-process smoke test is `tools/dircon_smoke.py`.** It launches the shipped
`qdomyos-zwift -no-gui -simulated-bike -ride …`, finds it with python's `zeroconf` — a
foreign stack, as the plan asks — resolves it, then opens TCP to the *discovered* address
and port and consumes the stream. Run against the Windows build it passes end to end: the
service resolves to `ELITE-AVANTI-01234-W.local.` on 36866, 0x2ACC reads back
`835400000ce00000`, `REQUEST_CONTROL` is acknowledged with `800001`, and `steady.ride`
arrives as 200 W at 90 rpm. It is deliberately not in CI — process orchestration is where
flakiness comes from, and this is the least load-bearing piece. Its goodbye check is behind
`--check-goodbye` because killing a process is not quitting one: neither `TerminateProcess`
nor `SIGTERM` unwinds the Qt event loop, so `aboutToQuit` never fires. The goodbye is covered
properly, and revert-checked, by the gtest above.

**Phase 5 — recording. Done.** `tools/qzlog2ride.py` turns a debug log into a fixture:
`t=<seconds> << <uuid> <hex>` for every frame the bike sent, `t=<seconds> >> <hex> # <what QZ
called it>` for every write, and — the part that makes it an oracle — the metrics QZ derived
from each frame, captured from the `Current …:` lines it logs immediately afterwards. Four
modes: extract, `--verify`, `--oracle`, `--stats`.

`tst/fixtures/recorded/ypbm-32min-ride.frames` is the first recording: a real 32-minute ride
on YPBM001264, 4610 frames and 387 writes.

*Accepted on* `--verify`, which replays the recorded frames through an FTMS decoder written
from the spec and compares them against what QZ logged at the time. Per field, because not
every metric QZ logs is one it read:

```
cadence     matches 1919/1919
heart       matches 1919/1919
resistance  matches 1919/1919
watts       matches 1919/1919
speed       differs 1895/1919 (frame 24.33, QZ 45.87) - QZ derives this rather than reading it
```

Every pass-through field replays exactly. Speed does not, and that is QZ working as
configured rather than a recording error: `speed_power_based` was on, so QZ ignored the
speed the bike reported and computed one from power. **The oracle is only valid alongside the
settings that produced it** — worth remembering before treating a recorded metric as ground
truth.

Three things the first recording found that a hand-written fixture could not have contained:

- **The bike splits Indoor Bike Data across two notifications a second, by field.** `0x01f5`
  in 18 bytes carries cadence, distance, resistance, instantaneous and average power and the
  energy triplet; `0x2a00` in 10 bytes carries speed, heart rate and elapsed time. Neither is
  a valid frame on its own and QZ parses both, which is why the fake bike's single `0x0264`
  frame is a deliberate simplification rather than a copy.
- **It sends three bytes more than it flags.** Every `0x2a00` frame is 10 bytes where its
  flags account for 7. It sets bit 13, which Indoor Bike Data does not define — the field it
  presumably means is bit 12, Remaining Time. QZ reads what it was told and ignores the rest,
  which is the right thing to do and now has a test case.
- **0x2AD9 arrives as a notification 387 times.** Those are the control point's indications
  answering QZ's writes, and they are in the recording alongside the writes that caused them.

One limitation, recorded rather than worked around: a `>>` line does not say which
characteristic was written, because `ftmsbike.cpp:174` does not log it. Writes are stored with
the UUID as `?`. See TODO.md.

**Phase 6 - Layer B, the frame harness. Done, with one part left to the trainer.**
`simulatedFtmsBike` in `tst/Devices/simulatedftmsbike.h`, the encoder in
`tst/Devices/ftmsframes.h`, and thirteen tests in `tst/Devices/TestFtmsFrameHarness.h`. The
shipped `ftmsbike` is now driven byte-exact, headless, with no radio - and
`ftmsbike::characteristicChanged`, nine hundred lines that had no test at all in either
direction, has one.

**The seams turned out to be six, not two.** Inbound was exactly as specified: a
`handleNotification(uuid, value, fromService)` the Qt slot delegates to, because a test cannot
build a `QLowEnergyCharacteristic` with a UUID in it. Outbound needed four rather than one,
because the write path defends itself at four levels and every one of them ultimately asks for
that same unbuildable object: `controlPointReady()`, `enqueueTargetValid()`,
`writeTargetReady()` and `performWrite()`. Plus `linkExists()` and `linkState()` for
`update()`'s gates. Every default is the code it replaced, and `WriteRequest` moved from
private to protected so a subclass can name the type it overrides on.

*Accepted on* two of the three criteria, and the third is not mine to sign off:

- **The suite passes unchanged.** 218 tests, 208 passed, 10 skipped before the seams; the same
  numbers after, exit code 0. With Layer B added it is 231 / 221 / 10.
- **The phase 3 assertions pass with `ftmsbike` as the bike end.** `FtmsBikeAsTheBikeEnd` puts a
  frame in one side of the whole stack - the real parser, the metrics, the notifier, DIRCON -
  and asserts the numbers on the wire at the other. Both bike ends now feed the same loop, which
  is what this phase was reordered to the end to make true.
- **A real ride behaving identically is untested.** It needs the trainer, and nothing here can
  stand in for it. The seams are mechanical and the suite says so, but that is an argument
  rather than evidence.

**It found two real bugs on its first run**, which is the return on the seams:

- **An unguarded `m_control->error()`** at the last line of the notification handler. The same
  null dereference this fork already fixed in `update()` - where the comment notes Qt 5 survives
  it and Qt 6 crashes on it - sitting in a second place, reachable by a notification arriving
  after a teardown has cleared the controller. It is the *last* statement of the handler, so the
  whole parse succeeds first and the crash looks like it came from nowhere near the frame.
- **0x2AD2 read past the end of the buffer.** The only length check was that the frame has a
  flags word; every field after it was read unguarded, so a frame whose flags promise more than
  it carries walks off the end - an assert in a debug Qt, silent garbage in a release one. The
  0x2ACE path in the same file already guards every field with `ensureBytesAvailable()`; 0x2AD2
  did not. Fixed with one up-front check computed from the flags, deliberately not thirteen
  inline ones: this is the hot path, and one arithmetic statement is easier to review and to
  keep in step. A frame *longer* than its flags describe is still accepted, because the real
  trainer sends one.

**The encoder is checked against the trainer, not against us.** An encoder bug and a parser bug
that agree cancel out and pass, so `ftmsframes.h` is validated by reproducing the two frames in
`tst/fixtures/recorded/ypbm-32min-ride.frames` byte for byte - including the one that sets bit
13, which Indoor Bike Data does not define, and carries three bytes more than it accounts for.
It caught itself applying the inverted bit-0 rule twice on the first run.

The test that earns the layer is `TheSameValuesSurviveADifferentFlagSet`: the same five readings
sent twice, once minimal and once surrounded by every other optional field, so every value sits
at a different offset. A reader that gets one width wrong reads cadence out of the resistance
slot and reports numbers that are plausible, wrong and silent.

Still out of reach: the branches gated on device-name flags - the three-byte Set Target
Resistance this trainer actually wants is one - because those flags are private to `ftmsbike`
and set by `deviceDiscovered()`, which needs a radio. That is a seventh seam, and it is in
TODO.md rather than done on the way past.

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

## No longer deferred: a real peripheral

Nothing in the phases above touches a radio. The layer that does is
`tools/fakebike-android/` — an Android app that advertises as **YPBM123456**, serves the
FTMS profile, and streams a `.ride` scenario as Indoor Bike Data. QZ discovers it, claims it
with the `ftmsbike` driver, and rides it. That covers discovery over BLE, the platform
Bluetooth backend, connection, subscription and the bytes QZ writes back — the areas this
fork changed most, and the ones that until now needed the trainer powered up to look at even
once.

It stays outside CI, for the reasons this section always gave: it needs two radios, and it is
nondeterministic in the way radios are. What changed is the cost. The plan expected the
cheapest starting point to be `QZ_ESP32/`, the Arduino sketch already in the tree; a phone
turned out to be cheaper still, because it needs no hardware, no flashing and no second
machine to build on — and it can put a UI in front of the scenario picker.

**It is the third player of the same format**, which is what that format was designed for.
`RideScenario.java` is a line-by-line port of the C++, both interpolation rules included, and
the build stages `tst/fixtures/rides/` into the APK rather than copying it — so a scenario
edited for the simulated bike is edited for the peripheral too. One scenario, three players:
the bike inside QZ, the Layer C tests, and now a radio.

One thing it reaches that nothing else can: **`dropout.ride` produces a real gap**. Phase 3
had to settle for asserting that a silence window freezes the DIRCON stream, because
`bikeProvider()` notifies on its own timer whatever the bike does. On the peripheral the
radio simply goes quiet, which is what a dropout actually is.

See `tools/fakebike-android/README.md` for the build, the ten-character name rule QZ enforces,
and the one difference from the real trainer that is known and deliberate.

## What the endgoal still does not cover

Worth being exact, because "CI runs the loop and asserts" is easy to hear as "CI tests QZ".

- **The radio.** Discovery over BLE, the WinRT backend, bonding, unpaired connection and
  reconnect backoff are untouched by anything in the phases above. They are reachable now,
  but by hand: `tools/fakebike-android/` is a real peripheral playing the same scenarios, and
  it is a bench instrument rather than a CI job. See the section below.
- **The real training apps.** See the end of Layer C: their undocumented rules are not
  knowable from here.
- **What is drawn.** The loop asserts on the metrics and the wire, not on the screen.
  This used to be a statement about `homeform` being unconstructable headlessly; since
  phase 7c-2b it is a smaller gap, because `RideState` is what the screen reads and
  `tst/UI/TestRideState.cpp` asserts on it directly. What remains unasserted is the QML
  itself — whether a bound value reaches a label — which still wants an offscreen run.
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
