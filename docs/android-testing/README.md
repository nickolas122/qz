# Android Testing Documentation

This directory contains procedures for testing QZ (qdomyos-zwift) with Android emulators and physical devices on the local network.

## Structure

- [qz.md](qz.md) - Installing and testing QZ on an emulator or device
- [zwift.md](zwift.md) - Testing the QZ <-> Zwift integration through Android notifications, without Bluetooth
- [automation.md](automation.md) - UI Automator and adb scripts for automated tests

## The fake-device toggles are dead in this fork (2026-08-17)

Read this before following any procedure below. `applewatch_fakedevice` and its treadmill,
elliptical and rower siblings **still appear in Experimental Features and still round-trip
to QSettings, but nothing instantiates a device from them.** Upstream's `fakebike`,
`faketreadmill`, `fakeelliptical` and `fakerower` were deleted in `2c39c5d`. Turning the
switch on now does nothing except stop QZ offering the first-run wizard.

The replacement is the **simulated bike** — `docs/fork/VIRTUAL-BIKE.md`:

| Setting | Value |
|---|---|
| `simulated_bike` | `true` — skips discovery entirely and rides a scenario |
| `simulated_bike_ride` | path to a `.ride` file; empty falls back to a built-in 150 W / 85 rpm ride |

Fixtures live in `tst/fixtures/rides/`. Every procedure below that writes
`applewatch_fakedevice=true` should write `simulated_bike=true` instead.

## Current status (2026-08-17)

| Piece | State | Notes |
|---|---|---|
| Build | VM `qz-android-build`, branch `lite-version` | `C:\VMs\qz-build\README.md`; `bash /c/VMs/qz-build/rebuild-and-test.sh` builds, copies and installs |
| Emulator | `qz_test`, Android 14 / API 34, x86_64 | Windows host, WHPX. No Bluetooth passthrough — the simulated bike is the only bike it can have |
| QZ | `lite-version`, installed and **verified riding a scenario** 2026-08-17 | verify the build with the settings dump at the head of `Documents/QZ/debug-*.log` |
| Zwift | Not installed | Zwift Companion is installed, but it is not valid for this test |

The older local-emulator notes below (`Pixel_8_API_36`, macOS paths) are from a different
machine and are kept for the adb technique rather than the specifics.

## Running the simulated bike on the emulator

Four steps, all from Git Bash on the Windows host with
`ADB=/c/Android/sdk/platform-tools/adb.exe`.

**1. Put a scenario on the device.** Nothing ships the fixtures, and the app's own private
directory needs no storage permission. base64 is only there to stop `adb shell` rewriting
line endings — verify with `md5sum` on both sides:

```bash
ADB="/c/Android/sdk/platform-tools/adb.exe"
DIR=/data/data/org.cagnulen.qdomyoszwift/files
B64=$(base64 -w0 tst/fixtures/rides/ramp.ride)
"$ADB" shell "run-as org.cagnulen.qdomyoszwift sh -c 'echo $B64 | base64 -d > $DIR/ramp.ride && md5sum $DIR/ramp.ride'"
```

**2. Point the settings at it, with the app stopped.** Editing the INI while QZ runs is
pointless — it rewrites the whole file on exit:

```bash
CONF="$DIR/.config/Roberto Viola/qDomyos-Zwift.conf"
"$ADB" shell am force-stop org.cagnulen.qdomyoszwift
"$ADB" shell "run-as org.cagnulen.qdomyoszwift sh -c 'cp \"$CONF\" \"$CONF.bak\" && sed -i \"/^simulated_bike/d\" \"$CONF\" && sed -i \"s|^\[General\]|[General]\nsimulated_bike=true\nsimulated_bike_ride=$DIR/ramp.ride|\" \"$CONF\" && sed -i \"s/^log_debug=false/log_debug=true/\" \"$CONF\" && head -4 \"$CONF\"'"
```

`log_debug=true` matters: without it QZ writes no log on Android at all and there is
nothing to read afterwards.

**3. Launch and look.**

```bash
"$ADB" shell am start -n org.cagnulen.qdomyoszwift/.CustomQtActivity
"$ADB" exec-out screencap -p > screen.png
"$ADB" shell "ls -lat /sdcard/Documents/QZ/*.log | head -3"
```

The log should contain `simulated bike enabled, skipping discovery` and
`simulatedbike playing … duration 90 s`. If it says *falling back to the built-in ride*,
the path is wrong — and the tiles will show a flat 150 W / 85 rpm, which is what that
fallback is for.

**4. Reach DIRCON from Windows** (the emulator NAT hides it otherwise, and mDNS discovery
cannot cross it — a client on the host has to be given the address):

```bash
"$ADB" forward tcp:37866 tcp:36866    # then connect to 127.0.0.1:37866
```

### Rename the device first, or the BLE virtual bike will not advertise

A BLE advertisement is 31 bytes and Qt's Android backend puts the **adapter's** name in it,
not the `QZ` that QZ asks for. The emulator ships as `sdk_gphone64_x86_64`, which does not
fit alongside the flags, TX power and service UUID, so advertising fails with
`QtBluetoothGattServer: Advertising failure: 1` (`ADVERTISE_FAILED_DATA_TOO_LARGE`) and QZ
reports *virtual bike bluetooth not connected*. QZ toasts a warning about it at startup —
`homeform.cpp:1086`, triggered by a name over nine characters or outside `[A-Za-z0-9 ]`.

**`settings put secure bluetooth_name` does not work.** The Bluetooth stack rewrites it
from the product model every time the adapter is enabled. Use the UI:

```bash
"$ADB" shell am start -a android.settings.DEVICE_INFO_SETTINGS
# tap "Device name", clear the field, type a short name, OK - then OK again on the
# "Your device name is visible to apps on your phone" confirmation, which is the tap that
# actually commits it.
"$ADB" shell settings get secure bluetooth_name      # should now be the short name
```

Do **not** cycle the adapter with `svc bluetooth` or `cmd bluetooth_manager` to make it take
effect. It is not needed, and it killed this emulator twice.

With a short name, a relaunch gives `onAdvertisingSetStarted() … status=0` and
`virtualbike::bikeProvider "virtual bike connected"`, writing Indoor Bike Data once a
second. The same applies to the tablet.

### What the emulator still cannot test


**A real central connecting over BLE.** With a short device name the virtual bike does
advertise and QZ writes to it, but nothing here scans for it — the emulator's Bluetooth is
a simulated controller with no second radio to be found by. Kinomap on the same device, or
a phone with a BLE scanner, is what would close that.

**The radio itself:** discovery, bonding, unpaired connection and reconnect backoff, on the
central side. That is the deferred layer in `docs/fork/VIRTUAL-BIKE.md` and needs hardware.

DIRCON is unaffected by all of this — it is a TCP server and the emulator's networking is
real.

## Initial Setup

### Local Emulator (Android Studio)

Available AVD: `Pixel_8_API_36`

Start it from the command line:
```bash
~/Library/Android/sdk/emulator/emulator -avd Pixel_8_API_36 -no-snapshot-load -gpu host
```

The `-gpu host` flag is required for Zwift testing. With the default `auto` GPU mode, the emulator may expose SwiftShader software rendering to Android, which is not enough for the Zwift app.

Wait until the emulator is ready:
```bash
~/Library/Android/sdk/platform-tools/adb wait-for-device shell getprop sys.boot_completed
# returns "1" when it is ready
```

Verify that the emulator is using host GPU acceleration:
```bash
~/Library/Android/sdk/platform-tools/adb shell cmd gpu vkjson \
  | grep -E '"deviceName"|"driverName"|SwiftShader'
```

For the tested macOS host, the expected renderer was `MoltenVK` on `Apple M2 Pro`. If the output mentions `SwiftShader`, the emulator is still using software rendering.

### Physical Devices on the Network

Connect through ADB over Wi-Fi:
```bash
adb connect <DEVICE_IP>:5555
adb devices
```

To enable wireless debugging on the device (Android 11+):
```text
Settings -> Developer options -> Wireless debugging -> Pair device with pairing code
```

## Password Notes

Account credentials (Google Play, Zwift, and similar services) should stay in the device password manager. Never store passwords in these files.
