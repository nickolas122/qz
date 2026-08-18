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

### What the emulator cannot test

**The BLE virtual bike does not advertise here.** The peripheral role starts and both GATT
services register, then `QtBluetoothGattServer: Advertising failure: 1` and QZ reports
*virtual bike bluetooth not connected*. The emulator's Bluetooth is a simulated
controller, so this is not evidence either way about a real device — it needs the tablet.
Everything else on this page is unaffected, because DIRCON is a TCP server and the
emulator's networking is real.

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
