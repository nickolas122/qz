# Moving QZ to Qt 6 (Route C)

> **Phase 1 is done, and it chose C1 (MSVC 2022 + official Qt 6). See
> `WINDOWS-QT6-PHASE1.md`.** That document also corrects three claims below — flagged inline where
> they occur — the largest being that Qt 6 does **not** require a move to CMake.

**Baseline:** `ble-hardening` @ `a10ffa82`, which is 52 commits ahead of `upstream/master` and 0 behind.
**Sources:** `upstream/qt6` @ `422d5739`, `qt/qtconnectivity` @ `v6.8.2` and `v5.15.2`, the fork's
`.github/workflows/main.yml`, and the mingw-w64 header history. Claims are tagged [verified] where
they come from reading one of those, [unverified] otherwise.

---

## Part 0 — Why this document exists now

`WINDOWS-WINRT-PHASE0.md` closed Routes A and B and recommended stopping. Two things have changed
since.

**A benefit Phase 0 never weighed.** Phase 0 judged four rows and refuted three. None of them was
"works with an unpaired device", because at the time nobody knew that mattered. On 13 Aug the trainer
was power-cycled, Windows kept its half of a bond the console had forgotten, and every CCCD write and
both control-point opcodes came back `Acesso negado` while reads kept being served from the OS GATT
cache. Discovery, battery level and the connected indicator all looked healthy; no data ever arrived.
That is now a recurring failure, worked around in `b3aef1d9` by dropping the pairing record and
sending the user to the pairing pane. [verified in the 21:45-21:55 logs]

Qt 6.8.2 still branches on `pairingStatus()` and still has `connectToUnpairedDevice()`
(`qlowenergycontroller_winrt.cpp:473-477`, `:606`). [verified] The Win32 backend cannot work
unpaired at all: it finds services through `SetupDiGetClassDevs(DIGCF_PRESENT)`, which only
enumerates devices Windows has already paired. [verified] So this failure class is a Win32 property,
and Qt 6 is where it stops.

**The console does not require a bond, and Android proves it.** QZ on Android drives this same bike
— streaming `2ad2` and writing the control point — over Qt's Android backend, which talks to
`BluetoothGatt` directly and writes CCCDs on an **unbonded** device. That is the standing premise of
`WINDOWS-BLE-HARDENING.md` Part I: works great on Android, flaky on Windows. So the peripheral's GATT
server does not demand an authenticated link for the FTMS characteristics, and since that is a
property of the peripheral it holds on any platform.

This removes the device-side half of the migration's risk outright. What remains is only whether
*Windows'* stack will do an unpaired GATT write once Qt asks it to — a question about the OS, not
about the bike. Worth stating plainly because it is the difference between "this might buy nothing"
and "this should work unless Windows itself refuses".

**The migration is not greenfield.** Upstream has a `qt6` branch with 261 commits on it.

Phase 0's own closing argument already pointed here: *"Qt 6 has no Win32 backend at all, so the
migration is eventually forced… That is an argument for planning Route C on its own schedule."*

---

## Part I — What upstream already did

`upstream/qt6` @ `422d5739`, branched Feb 2026, last synced with master on 2026-06-09 and **115
commits behind** as of today. [verified] It periodically merges master rather than rebasing, so it is
a long-lived integration branch, not a spike.

Outside translations it touches **203 files, 152 of them .cpp**, of which **126 are under
`src/devices/`**. [verified] The structural pieces:

| Change | Detail |
|---|---|
| Build system | `CMakeLists.txt`, `src/CMakeLists.txt` (516 lines), `CMakePresets.json` — moves to CMake ⚠️ **wrong: every job on that branch still builds with qmake; the CMake files are unused by its CI. Phase 1 §6** |
| Qt version | `find_package(Qt6 …)`, CMake ≥ 3.23, project version 2.20.5 |
| Compat shim | Uses `Qt6Core5Compat` (so `QRegExp`/`QTextCodec` survive rather than being ported) |
| Android | `build.gradle` changes, `configure-android-multiabi.{sh,bat}` |
| CI | `main.yml` largely rewritten |

**Its Windows story is unsound and must not be copied.** `window-build` on that branch installs Qt
**6.8.2** with `arch: win64_mingw81` and then copies `qt-patches/windows/5.15.2/binary/mingw64/*.*`
— Qt **5** DLLs — into a Qt 6 output directory. [verified] That step is vestigial at best. Part III
explains why the mingw choice is the real problem.

**Implication for us.** The fork is 52 commits ahead of master with 96 diverged files, including the
DIRCON server refactor, the BLE hardening work and the qmdnsengine changes. Merging that onto a
branch which is itself 115 commits behind master is the wrong shape. Take upstream's qt6 work as a
**reference and a source of patches**, not as a base to branch from.

---

## Part II — The device migration is mostly mechanical

Sampling the qt6 diff across device files shows four substitutions repeated over and over
[verified, e.g. `src/devices/apexbike/apexbike.cpp`]:

```cpp
// 1. the enum was renamed
- if (state == QLowEnergyService::ServiceDiscovered)
+ if (state == QLowEnergyService::RemoteServiceDiscovered)

// 2. + 3. the overloaded error signal became its own name
- static_cast<void (QLowEnergyService::*)(QLowEnergyService::ServiceError)>(&QLowEnergyService::error)
+ &QLowEnergyService::errorOccurred
- static_cast<void (QLowEnergyController::*)(QLowEnergyController::Error)>(&QLowEnergyController::error)
+ &QLowEnergyController::errorOccurred

// 4. the enums became scoped
- QBluetoothUuid::ClientCharacteristicConfiguration
+ QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration
```

All four are `sed`-able and verifiable by compilation. The non-mechanical part is small but real:
upstream also added null guards (`if (!m_control) return;`) at the top of `update()` in many devices,
which is a behaviour change, not a rename.

Beyond the device layer, the known Qt 6 items QZ will hit are `QtAndroidExtras` being gone
(`QAndroidJniObject` → `QJniObject`, `QtAndroid::androidActivity()` → `QNativeInterface`), which the
qt6 diff shows ~90 occurrences of, and `QLowEnergyController`'s constructors being private in favour
of `createCentral()`/`createPeripheral()`. [verified for the counts; the constructor change is from
Qt's own "Changes to Qt Bluetooth" page]

---

## Part III — Windows Bluetooth on Qt 6 is the decisive constraint

Qt 6.8.2 selects the backend like this [verified, `src/bluetooth/configure.cmake:56-59` and
`src/bluetooth/CMakeLists.txt:242`]:

```cmake
qt_feature("winrt_bt" PRIVATE
    LABEL "WinRT Bluetooth API"
    CONDITION WIN32 AND TEST_winrt_bt
)
```

```cmake
elseif(QT_FEATURE_winrt_bt)
    …qlowenergycontroller_winrt.cpp…
else()
    …qlowenergycontroller_dummy.cpp…      # compiles, links, does nothing
```

The condition is `WIN32`, not MSVC — same shape as Qt 5. But when the compile test fails there is no
Win32 fallback any more: **you get the dummy backend.** The Qt Company's official mingw packages are
built where that test does not pass, which is why "Qt 6 + mingw + Bluetooth" is reported as a
non-functional dummy backend and MSVC is the standard advice. [**verified** — `QT_FEATURE_winrt_bt`
is `-1` in the shipped 6.8.2, 6.9.3 and 6.10.3 mingw packages and in llvm-mingw, `1` in msvc2022,
and the DLL import tables agree. Phase 1 §1]

That gives two sub-routes:

| | **C1 — MSVC 2022 + official Qt 6** | **C2 — mingw + self-built qtconnectivity** |
|---|---|---|
| Bluetooth backend | WinRT, out of the box | WinRT **if** the config test passes |
| CI job exists | **Yes** — `window-msvc2022-build`, already on Qt 6.8.2 | No, but the from-source machinery does |
| Keeps mingw toolchain | No | **Yes** |
| Costs | vcpkg + protobuf, VC++ runtime, install-layout break, exe-only upgrades invalid | A Qt module build per CI run (already cached) ⚠️ **wrong by an order of magnitude — Phase 1 §3-4** |
| Confidence | High | Medium |

**Chosen: C1.** The C2 column's cost estimate did not survive measurement. Qt gates WinRT on
`Q_CC_MSVC` inside `qfunctions_winrt_p.h`, so the LE controller loses `QWinRTFunctions::await` under
GCC, and mingw-w64's `windows.networking.sockets.h` is a 126-line stub that breaks every translation
unit including `qbluetoothserver_p.h`. C2 is a maintained fork of Qt's Bluetooth module, not a build
of it. Full measurement in `WINDOWS-QT6-PHASE1.md`.

**C2 deserves a serious look, and this is new.** Route B died in Qt 5 for one specific reason:
`qbluetoothutils_win.cpp` redefines `QEventDispatcherWinRT::runOnXamlThread` — a member of a
`Q_CORE_EXPORT`ed class — and silences the resulting MSVC C4273, which GCC rejects outright.
[verified] **That file does not exist in Qt 6.** The 6.8.2 WinRT source list contains only
`qbluetoothutils_winrt.cpp`, with no `CLASSIC_APP_BUILD` stand-in. [verified] The compile test itself
is unchanged between 5.15.2 and 6.8.2 — WRL plus the `windows.devices.*` ABI headers, no C++/WinRT
[verified of the *test*; ⚠️ **the backend is a different story — seven of its files include
`<winrt/…>`, and it is not what blocks the route. Phase 1 §3**] — and mingw-w64 has shipped
`windows.devices.bluetooth.h` since **2023-07-13** [verified
from its git history]; the MSYS2 GCC 16.2 on this machine has it, along with `libbthprops.a`
[verified locally].

So the thing that killed Route B is gone, and **the fork already builds qtconnectivity from source in
CI** (one cached step, keyed on the patch files). If C2 works, Qt 6 arrives without vcpkg, without a
CRT change, and without breaking the install layout.

---

## Part IV — The stripping lever

Stated intent: QZ should become a pure adapter for the one FTMS bike (`YPBM001264`), with everything
else removed.

That is not a side quest here — it is the largest single cost reduction available. The device layer
is **126 of the 203 migrated files**, and the fork carries **148 device directories** against one
that matters. `ftmsbike.cpp` is 2776 lines; `bluetooth.cpp`, the factory that instantiates every
device, is 4822.

**Sequencing matters, and the cheap order is counter-intuitive.** Stripping first means porting ~1
device instead of 126, and it makes every later compile error attributable. Stripping is also a
pure-deletion change that can be validated on the *current* Qt 5 build, where the bike is known to
work — so it is testable before any Qt risk is taken on.

The caveat: deleting devices means diverging from upstream hard enough that future merges from master
become mostly conflict resolution in `bluetooth.cpp`. Given the fork is already 52 commits ahead and
explicitly never upstreams (fork-only), that cost is largely already paid — but it should be a
deliberate choice, not a side effect.

---

## Part V — Phased plan

Each phase ends somewhere it is safe to stop.

**Phase 1 — Answer the one question that decides C1 vs C2. (~half a day, no port) — DONE**
It is the dummy, in every mingw package from 6.8.2 to 6.10.3 and in llvm-mingw. C2 was then taken as
far as it goes: five obstacles yield to shims, two do not. **C1.** `WINDOWS-QT6-PHASE1.md` has the
measurements; nothing was ported, and the probe needed no CI — the answer is readable in the shipped
config headers.

**Phase 2 — Strip, on Qt 5. (Independent of everything above)**
Reduce the device layer to `ftmsbike` plus whatever `bluetooth.cpp` needs to reach it. Validate
against the real bike on the current, known-good Qt 5 mingw build. **Deliverable: a working QZ that
supports one bike.** This is worth doing even if the Qt 6 work never happens.

**Phase 3 — Port, with the device zoo already gone.**
Apply the Part II substitutions and take upstream qt6's non-device changes as reference patches
rather than merging its branch. Expect the real work to be `bluetooth.cpp`, `homeform.cpp`, the
virtual devices and `webserverinfosender.cpp`, not the device. **Keep qmake** — Phase 1 §6 found
that upstream's qt6 branch builds every target with it, so the CMake migration is a separate,
optional change and not part of this phase.

**Phase 4 — Verify against the same bike.**
Re-run the `WINDOWS-BLE-HARDENING.md` §1 list: exactly one `all services discovered!`, `2ad9`
indication subscribed, control granted, `2ad2` streaming on cold launch and on relaunch untouched.
Then the two Qt 6-specific questions:
- **Remove the pairing entirely and confirm QZ still connects and writes.** This is the whole point
  of the migration. Part 0 establishes the bike is willing, so a failure here indicts Windows or Qt,
  not the console — and the Android build is the control case to compare against.
- Do the `ATT_ATTRIBUTE_NOT_FOUND` warnings persist? Phase 0 predicted they would, because every Qt
  enumeration call uses the `Cached` overload on both backends. Qt 6 does not change that by itself.

---

## Risks

~~**The mingw/dummy-backend claim is unverified.**~~ **Settled.** It is the dummy, measured on the
shipped packages; see Phase 1 §1. The route is C1.

**`Qt6Core5Compat` is a trap door.** Upstream leans on it. It makes the port compile sooner and
leaves `QRegExp` in the codebase indefinitely. Acceptable as a migration aid; worth a follow-up.

**The QZ Bluetooth patches do not carry forward.** The fork patches both Qt 5 WinRT backends
(`_winrt.cpp:363` and `_winrt_new.cpp:1670`) and `qlowenergycontroller_win.cpp`. Qt 6 has a single,
rewritten WinRT controller, so those patches must be re-derived or shown to be unnecessary. Phase 0
already found the Win32 descriptor patch has no WinRT equivalent need. **The `/* QZ rviola` edit now
has an answer: it is obsolete on the unpaired path.** Qt 6.8.2 keeps the probe read it removes, but
only in `connectToPairedDevice()`; `connectToUnpairedDevice()` has no probe at all. Phase 1 §7.

**Android is not free.** `QtAndroidExtras` is gone and the fork's Android BLE peripheral patches are
against Qt 5.15.0. If Android matters, it is a second migration with its own patch set; if the goal
is a Windows adapter for one bike, say so explicitly and let the Android build lapse.

**`window-msvc2019-build` stays disabled** regardless. It was Phase 1 of the *Route A* plan, and
there is no Route A.

---

## Open questions

- ~~Does official Qt 6.8.2 mingw ship a working Bluetooth backend, or the dummy?~~ **The dummy.**
  So does 6.9.3, 6.10.3 and llvm-mingw. Phase 1 §1.
- ~~Does `TEST_winrt_bt` pass under MSYS2 GCC 16.2 against Qt 6.8.2 sources?~~ **It compiles and
  fails to link, on one WRL template mingw-w64 declares but never defines.** With that supplied it
  passes — and then the backend behind it still does not build. Phase 1 §2-4.
- ~~Does the `/* QZ rviola` WinRT patch have a Qt 6 equivalent, or is it obsolete?~~ **Obsolete
  where it matters** — the probe read it deletes exists only on the paired path. Phase 1 §7.
- Is Android in scope after the strip?
- ~~Does this console accept unauthenticated writes?~~ **Answered: yes.** See below.
- Does the fork need a patched qtconnectivity on Qt 6 at all? Only the stale-cache force-read is a
  candidate, and it is now an MSVC build if it is wanted. Deferred to Phase 4's logs.
