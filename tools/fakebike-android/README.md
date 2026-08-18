# The fake bike — an FTMS trainer that is not there

An Android app that turns a phone into the bike. It advertises as **YPBM123456**, serves the
Fitness Machine profile over BLE, and streams a `.ride` scenario as Indoor Bike Data. QZ
discovers it, claims it with the `ftmsbike` driver, and rides it — over a real radio, with no
trainer in the room.

This is the layer [`docs/fork/VIRTUAL-BIKE.md`](../../docs/fork/VIRTUAL-BIKE.md) defers under
*"a real peripheral"*. Everything Layer C covers stops at QZ's own output; this covers the
half nothing else reaches — **discovery over BLE, the platform Bluetooth backend, connection,
subscription, and the bytes QZ writes back**. Those are precisely the areas this fork has
changed most, and until now the only way to exercise them was to power up the trainer.

It cannot run in CI and it is not trying to. It is a bench instrument.

## The name is load-bearing

QZ picks its driver from the advertised name, and for this bike the test is exact
(`src/devices/bluetooth.cpp:894`):

```cpp
b.name().toUpper().startsWith("YPBM") && b.name().length() == 10
```

`YPBM123456` is ten characters. Nine or eleven and QZ will see the device, decline to claim
it, and say nothing useful about why.

**The app borrows the phone's Bluetooth name to do this.** On Android the advertised local
name *is* the adapter name — an app cannot set it per-advertisement — so Play renames the
adapter and Stop puts the old name back. Force-stopping the app instead leaves the phone
called YPBM123456, and you have to fix it in Settings → About → Device name. Press Stop.

Two things about that rename cost a debugging round, and both are now handled:

- **`setName()` is asynchronous.** It goes through binder to the Bluetooth process, and the
  advertising payload is built from whatever the name is when `startAdvertising()` runs.
  Starting immediately is a race; the app now waits for the adapter to report the new name
  before going on the air, and says so if it never does.
- **The name has to be in the advertisement, not only in the scan response.** A scanner
  receives a scan response only if it is scanning *actively*, and plenty are not. With the
  name in the scan response alone, QZ on Windows saw an unnamed address advertising `0x1826`
  and looked straight past it — a device with no name cannot match the name QZ selects its
  driver by. The status line shows the name actually on the air, so this is visible rather
  than silent.

**If your real trainer is switched on, turn it off or tell QZ which one you want.** QZ claims
the first `YPBM*` device it discovers and stops scanning, so a powered-up YPBM001264 wins the
race about half the time. Setting *filter device* to `YPBM123456` in QZ's settings removes the
ambiguity.

## Build

The APK is built on the Android build VM (`C:\VMs\qz-build\README.md`), which already has a
JDK, an Android SDK and Gradle. This app needs none of QZ's Qt toolchain — only those.

```bash
bash tools/fakebike-android/build-on-vm.sh            # -> C:\VMs\qz-build\output\fakebike-debug.apk
bash tools/fakebike-android/build-on-vm.sh --install  # ...and adb install it
```

The script starts the VM if it is powered off, syncs this directory, builds `assembleDebug`,
and copies the APK back. A clean build takes about a minute.

It is an ordinary Gradle project, so `./gradlew assembleDebug` or opening it in Android Studio
works too — those paths are just not the ones exercised here.

## Install

```bash
C:\Android\sdk\platform-tools\adb.exe install -r C:\VMs\qz-build\output\fakebike-debug.apk
```

Or copy the APK to the phone and tap it. It is signed with the standard debug key, so
"install from unknown sources" has to be allowed for whatever app hands it over.

## Use

Pick a ride, press **Play**. The label underneath shows what is going out on the wire:

```
t=21s   0 W   0 rpm   0.0 km/h   res 0   hr 152
```

The screen is kept on while it plays — a trainer that stops streaming when the screen dims is
not a trainer.

The scenarios come from `tst/fixtures/rides/`, staged into the APK by the build. They are the
same files the simulated bike and the Layer C tests play, not a copy, so a scenario edited for
one is edited for all three. `coast.ride` is the one to reach for first: it drives cadence,
power and speed to zero mid-ride while heart rate keeps moving, which is a case real trainers
get wrong and a good check that QZ is reading the stream rather than inventing it.

`dropout.ride` is worth more here than anywhere else. On this peripheral a silence window is a
**genuine gap in the notification stream** — the radio goes quiet. The in-process DIRCON tests
cannot produce that, because there QZ's own timer keeps pushing the last values whatever the
bike does.

## What it serves

| | |
| --- | --- |
| Service | `0x1826` Fitness Machine |
| `0x2ACC` | Fitness Machine Feature, read — cadence, resistance, heart rate, power; resistance, power and simulation parameters accepted as targets |
| `0x2AD6` | Supported Resistance Level Range, read — 1.0 to 32.0 in steps of 1.0 |
| `0x2AD3` | Training Status, read |
| `0x2AD2` | Indoor Bike Data, notify — one frame a second |
| `0x2AD9` | Control Point, write + indicate |

Indoor Bike Data goes out with flags `0x0264` — instantaneous speed, cadence, resistance
level, instantaneous power and heart rate:

```
64 02 | speed (0.01 km/h) | cadence (0.5 rpm) | resistance | power (W) | hr | 00
```

Speed is derived from power using the model ported from `metric::calculateSpeedFromPower`,
with QZ's default rider weight, so a rider sees the same number here as from the simulated
bike playing the same file.

### The control point, including this bike's quirk

`REQUEST_CONTROL`, `RESET`, `START_RESUME` and `STOP_PAUSE` are acknowledged.
`SET_TARGET_POWER` puts the bike into ERG and it converges on the target using the scenario's
`erg_lag`. `SET_INDOOR_BIKE_SIMULATION_PARAMS` sets the grade, which changes the speed the
same power produces.

`SET_TARGET_RESISTANCE` accepts **both** spellings, and that is the point. `ftmsbike.cpp:598`
sends three bytes to a YPBM — opcode plus the level times ten as a 16-bit little-endian value
— where ordinary FTMS is two bytes with the level as a single byte. Accepting both is what
makes this a mock of *this* bike rather than of a generic trainer, and it means QZ's
YPBM-specific write path is exercised for real.

## Where it differs from the real bike

The real YPBM trainer splits its data across **two** notifications a second, one with flags
`0x01f5` and one with `0x2a00`, rather than the single frame this sends. Reproducing that byte
for byte would exercise a parse path nothing else does, and it is worth doing — but it belongs
with Layer B and recorded fixtures, where the frames come from a capture rather than from a
guess. This peripheral exists to test the radio, and a frame QZ certainly understands is the
right thing to test the radio with.

## The emulator advertises, but it is not a radio

The app runs on the API 34 emulator and reaches `advertising as YPBM123456` with
`onAdvertisingSetStarted status=0`, which is enough to smoke-test that it installs, parses the
scenarios and starts cleanly. Nothing can *see* it there: there is no radio behind the
emulated stack. Testing QZ against it needs the APK on a real phone and QZ on a second
machine.

Two things carry over from QZ's own Android notes and cost time when forgotten:

- **A long Bluetooth name breaks advertising.** An advertisement is 31 bytes and Android
  reports overflow as bare error code 1, `ADVERTISE_FAILED_DATA_TOO_LARGE`. Here it is flags
  (3) plus the 16-bit service UUID (4) plus a ten-character name (12), which is 19 — but only
  because the adapter has been renamed by then. If the rename fails, the phone's own name goes
  in and a long one overflows.
- **Do not cycle the adapter with `svc bluetooth` or `cmd bluetooth_manager`** to make a rename
  take effect. It killed the emulator outright twice. Nothing here needs a cycle.
