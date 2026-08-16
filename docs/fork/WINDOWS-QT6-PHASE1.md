# Phase 1 — Which toolchain, measured

**Scope:** the Phase 1 gate in `WINDOWS-QT6-PLAN.md` Part V — *"Answer the one question that decides
C1 vs C2"*. Measure; port nothing.
**Method:** the official Qt binaries were downloaded and read directly (`aqt install-qt`, then the
shipped config headers and the DLL import tables). `qt/qtconnectivity` `v6.8.2` was then configured
and built from source against the official mingw Qt 6.8.2, using this machine's MSYS2 GCC 16.2.
Everything below is measured on this machine unless marked otherwise.

**Verdict: C1 — MSVC 2022 with official Qt 6.**

C2 is not "Qt plus a patch". Qt 6's WinRT support is gated on `Q_CC_MSVC` inside a QtCore private
header, and mingw-w64 does not ship the ABI declarations the rest of the backend needs. Taking C2
means maintaining a fork of Qt's Bluetooth module against an explicit compiler gate — not the "one
cached CI step" the plan assumed.

---

## 1. The dummy backend is real, and it is not a packaging accident

Part III called this "the single most important thing to verify empirically". It is now verified,
two independent ways, on the shipped binaries.

| Official Qt build | `QT_FEATURE_winrt_bt` | `Qt6Bluetooth.dll` imports | size |
|---|---|---|---|
| 6.8.2 `win64_mingw` | **-1** | KERNEL32, msvcrt, libstdc++-6, Qt6Core | 345,272 |
| 6.8.2 `win64_msvc2022_64` | **1** | `api-ms-win-core-winrt-l1-1-0`, `…-winrt-string-l1-1-0`, ole32, OLEAUT32, + CRT | 826,520 |
| 6.8.2 `win64_llvm_mingw` | **-1** | libc++, CRT only | 321,176 |
| 6.9.3 `win64_mingw` | **-1** | — | — |
| 6.10.3 `win64_mingw` | **-1** | — | — |

The feature macro is read from
`include/QtBluetooth/6.8.2/QtBluetooth/private/qtbluetooth-config_p.h` in each package; the imports
from `objdump -p`. The MSVC row is the control: it proves the macro means what it says, and that a
real WinRT backend is visible from outside as a WinRT import table.

Two things follow that the plan did not have. **llvm-mingw is not an escape hatch** — Qt ships a
dummy backend there too, so "mingw" is not shorthand for "GCC". And this is **not a transient gap**:
it is unchanged through 6.10.3, the newest build checked.

## 2. Why the mingw package fails its own test

The proximate cause is one symbol. Qt's own `config.tests/winrt_bt/main.cpp`, compiled under MSYS2
GCC 16.2 exactly as Qt compiles it:

```
$ g++ -std=c++17 main.cpp -o winrt_bt_test.exe -lruntimeobject
undefined reference to `Microsoft::WRL::Wrappers::HString::MakeReference<46u>(wchar_t const (&) [46u])'
```

It **compiles**. `wrl.h`, `windows.devices.enumeration.h`, `windows.devices.bluetooth.h`,
`IGattDeviceService3`, `IBluetoothLEDevice` — all of it parses. It fails at link, on one template
that mingw-w64 declares and never defines (`mingw64/include/wrl/wrappers/corewrappers.h:193` and
`:196`). Supply the missing definition in twelve lines and the test links and runs, exit 0.

So the plan's reading of Part III was right about the test and wrong about what the test proves. The
test is not representative of the backend, which is the subject of §3.

## 3. Forcing the feature on: what it takes to get most of the way

Configuring `qtconnectivity` `v6.8.2` with `-DTEST_winrt_bt=ON` against the official mingw Qt 6.8.2
and building `Bluetooth` needs, in order of discovery:

| # | Obstacle | Answer |
|---|---|---|
| 1 | `HString::MakeReference` undefined (§2) | 12-line definition shim |
| 2 | `winrt/base.h` not found — the backend uses **C++/WinRT**, not only WRL | MSYS2 ships `mingw-w64-x86_64-cppwinrt` 2.0.250303.1 — 339 headers, all the ones Qt includes |
| 3 | `C++/WinRT requires coroutine support` | build the module at C++20; Qt modules default to C++17 |
| 4 | `exception handling disabled, use '-fexceptions'` | `EXCEPTIONS` on `qt_internal_add_module`; Qt modules default to `-fno-exceptions` |
| 5 | `QtCore/private/qfactorycacheregistration_p.h: No such file` | not installed in the mingw package. Its body is behind `QT_CONFIG(cpp_winrt)`, and the mingw QtCore reports `QT_FEATURE_cpp_winrt -1`, so a copy of the upstream header compiles to nothing — at the cost of the factory-cache cleanup Qt added as a Windows SDK bug workaround |

With all five in place, **three of the nine WinRT sources compile clean**:
`qbluetoothdevicediscoveryagent_winrt.cpp`, `qbluetoothdevicewatcher_winrt.cpp` and
`qbluetoothlocaldevice_winrt.cpp` — which are, notably, the three that lean hardest on C++/WinRT (19,
21 and 40 `winrt::` uses). GCC compiles C++/WinRT fine. That is worth stating plainly, because it is
the opposite of the usual assumption and it is not what blocks this route.

## 4. The two that do not yield

**Qt gates WinRT on the compiler, in QtCore.**
`include/QtCore/6.8.2/QtCore/private/qfunctions_winrt_p.h`, shipped in the mingw package, is one
`#if` around its whole body:

```cpp
#if defined(Q_OS_WIN) && defined(Q_CC_MSVC)
```

Under GCC the file is empty, so `RETURN_IF_FAILED`, `Q_ASSERT_SUCCEEDED` and
`QWinRTFunctions::await` do not exist. `qlowenergycontroller_winrt.cpp` — the GATT controller, the
only file in this module QZ actually depends on — uses `QWinRTFunctions::await` throughout, on both
the paired and unpaired connect paths. This is not an oversight in a package; it is Qt declaring in
source that WinRT support is MSVC-only. Identical in the header as installed and in `qt/qtbase` at
`v6.8.2`.

**mingw-w64's `windows.networking.sockets.h` is a stub.** 126 lines, zero interfaces — it declares
`SocketProtectionLevel` and stops. `qbluetoothserver_p.h` uses `ABI::…::IStreamSocket`,
`IStreamSocketListener`, `IStreamSocketListenerConnectionReceivedEventArgs` and
`EventRegistrationToken` unconditionally inside `#ifdef QT_WINRT_BLUETOOTH`, and that header is
pulled into **every** translation unit that touches `QBluetoothServer` — including
`qlowenergycontroller_winrt.cpp`. So the Bluetooth Classic half of the backend does not merely fail
to build; it poisons the LE half by inclusion.

QZ needs no RFCOMM, so the shape of a fix is clear enough: introduce an LE-only variant of
`QT_WINRT_BLUETOOTH`, swap the four Classic sources for their `_dummy`/`_p` counterparts, and hand
the LE controller a GCC implementation of `QWinRTFunctions::await`. That is a patch set against
Qt's own source selection and against a deliberate compiler gate, re-derived on every Qt update. It
is a different order of commitment from the qthttpserver-style source build the plan had in mind,
and it buys a toolchain, not a capability.

## 5. What C1 costs, concretely

- **protobuf via vcpkg.** `src/qdomyos-zwift.pri:34` is `win32:!mingw:LIBS += -llibprotobuf …`, so
  the MSVC build links protobuf where mingw does not. The existing `window-msvc2022-build` job
  already carries the vcpkg step and caches it.
- **No local builds on this machine, until VS Build Tools are installed.** There is no
  `Microsoft Visual Studio` directory on this box. Local mingw builds became possible when Smart App
  Control was turned off; C1 gives that up unless the Build Tools go on. That is the one real loss,
  and it is a one-time download rather than a standing maintenance cost.
- **VC++ runtime DLLs** ship instead of `libstdc++-6`/`libgcc_s_seh-1`/`libwinpthread-1`.
- **The install must be replaced once, whole.** Unavoidable in any Qt 5 → 6 move: every DLL is
  renamed `Qt5*` → `Qt6*`, so `windows-exe-only` is invalid across this change no matter the
  toolchain.
- `-lbthprops` for `BluetoothRemoveDevice()` (`windowsblebond.cpp`) is a Windows SDK import library
  and is available to MSVC unchanged.

## 6. Corrections to `WINDOWS-QT6-PLAN.md`

**Qt 6 does not require CMake.** Part I reads the CMake files on `upstream/qt6` as a move to CMake.
The files are there, but **every job in that branch's `main.yml` builds with qmake** — `qmake`/`make`
on Windows-mingw, Android and iOS, `qmake`/`nmake` on MSVC, `qmake6`/`make` on Raspberry Pi. The
fork's own `window-msvc2022-build` is likewise `qmake` + `nmake` on Qt 6.8.2. Phase 3 can keep
`qdomyos-zwift.pri` and change the Qt version; the CMake migration is optional and separable.

**Part III's "no C++/WinRT" is true of the config test only.** Seven files in the 6.8.2 backend
include `<winrt/…>`. The test does not, which is exactly why the test is a poor proxy.

**upstream/qt6's `window-build` cannot install its Qt.** It asks for `version: '6.8.2'` with
`arch: win64_mingw81`. The arches that exist for 6.8.2 are `win64_llvm_mingw`, `win64_mingw`,
`win64_msvc2022_64`, `win64_msvc2022_arm64_cross_compiled`. `win64_mingw81` is a Qt 5 name. This is
a sharper version of the plan's "its Windows story is unsound": that job never had a Qt to build
against, which is consistent with it also carrying the vestigial copy of Qt 5 DLLs.

## 7. Two open questions closed on the way

**The `/* QZ rviola` patch has no equivalent on the unpaired path.** In Qt 5.15.2 both WinRT
backends force a connection by reading the first readable characteristic
(`ReadValueWithCacheModeAsync`), and treat its failure as a connection error; QZ comments that block
out and connects immediately. Qt 6.8.2 keeps that probe read — but only inside
`connectToPairedDevice()` (`qlowenergycontroller_winrt.cpp:525-600`). `connectToUnpairedDevice()`
(`:606`) just retries `GetGattServicesAsync` until it succeeds and then emits `connected`. So going
unpaired removes the reason for the patch rather than porting it. If pairing is kept, the paired
path still has the probe and the patch still applies.

**`ATT_ATTRIBUTE_NOT_FOUND` will persist, as Phase 0 predicted.** In 6.8.2 the enumeration calls are
the plain `GetGattServicesAsync` / `GetCharacteristicsAsync` / `GetDescriptorsAsync` overloads, which
default to `BluetoothCacheMode_Cached`. Only *value* reads pass `BluetoothCacheMode_Uncached`
(`:220`, `:302`, `:574`, `:1402`, `:1584`). Qt 6 does not change the stale-cache behaviour by
itself. A Qt 6 equivalent of the fork's Win32 force-read patch would mean
`GetGattServicesWithCacheModeAsync` and friends — possible, but it puts a patched qtconnectivity back
on the table, this time built with MSVC.

## What Phase 1 did not answer

- Whether Qt 6.8.2's WinRT backend actually writes CCCDs on an unpaired device on *this* Windows
  install. That is Phase 4 and needs the bike. Part 0 of the plan establishes the console is
  willing; the code path exists and is short (§7); nothing here tests it.
- Whether QZ needs a patched qtconnectivity on Qt 6 at all. Deferred until the port runs and the
  logs say.

## Reproducing

```sh
# the four packages read in §1 — each is a ~4 MB single-archive download
python -m aqt install-qt windows desktop 6.8.2 win64_mingw -m qtconnectivity \
    --archives qtconnectivity --outputdir <dir> -E "C:/Program Files/7-Zip/7z.exe"
cat <dir>/6.8.2/mingw_64/include/QtBluetooth/6.8.2/QtBluetooth/private/qtbluetooth-config_p.h
objdump -p <dir>/6.8.2/mingw_64/bin/Qt6Bluetooth.dll | grep "DLL Name"

# the config test of §2
git clone --depth 1 --branch v6.8.2 https://code.qt.io/qt/qtconnectivity.git
g++ -std=c++17 qtconnectivity/config.tests/winrt_bt/main.cpp -o t.exe -lruntimeobject

# the gate of §4, in the package itself
sed -n '20p' <qt>/include/QtCore/6.8.2/QtCore/private/qfunctions_winrt_p.h
wc -l /c/msys64/mingw64/include/windows.networking.sockets.h   # 126
```
