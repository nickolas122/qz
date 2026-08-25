# Getting a Windows build of this fork

There are two routes: **locally**, which works now, and **GitHub Actions on the
fork**, which is what produces the shipped artifacts.

Smart App Control used to make the local route impossible - it blocked
`C:\msys64\mingw64\bin\g++.exe` outright, so `qmake` stopped with
`Project ERROR: Cannot run compiler 'g++'` and nothing linked. The machine owner
turned it off on 2026-08-13. That is permanent (re-enabling requires reinstalling
Windows), and it also un-blocked the unsigned native Python wheels that made
`py7zr` fail to import `pybcj` - so `aqt` no longer strictly needs
`-E "C:\Program Files\7-Zip\7z.exe"`, though passing it does no harm.

**The same error message now means something else entirely.** See
[Building locally](#building-locally) - the first thing it catches is PATH order.

## Getting a build

1. Actions must be enabled once per fork - GitHub disables them on forks by
   default: <https://github.com/nickolas122/qz/actions>
2. Actions → **CI** → *Run workflow* → pick the branch.
3. When `window-build (false)` goes green, take the **`windows-binary-no-python`**
   artifact from the run summary, unzip, run `output\qdomyos-zwift.exe`.

`workflow_dispatch` runs the workflow as it exists **on the selected branch**, so a
change to `.github/workflows/main.yml` only affects dispatches started after it is
pushed. A run already in flight keeps the workflow and commit it started with.

The `Secrets` step is `if: github.ref == 'refs/heads/master'`, so on any other
branch `src/secret.h` is never written. That is harmless - every use of it is
guarded by `#if __has_include("secret.h")`; only the Strava, Peloton and SMTP keys
are lost.

### Which artifact

| Artifact | Toolchain | Built from |
|---|---|---|
| `windows-binary-no-python` | mingw, **Qt 5.15.2** | the branch you dispatch |
| `windows-exe-only` | mingw, **Qt 5.15.2** | the branch you dispatch - the stripped `.exe` alone |
| `windows-qt6-binary` | MSVC 2022, **Qt 6.8.2** | the branch you dispatch, from `window-qt6-build` |
| `windows-qt6-exe-only` | MSVC 2022, **Qt 6.8.2** | the same, `.exe` alone |
| `windows-msvc2022-binary-no-python` | MSVC 2022, **Qt 6.8.2** | upstream nightly only, and from `refs/pull/1508/head` - a PR branch, not master |

**The two toolchains are not interchangeable at runtime.** Only the Qt 6 / MSVC
artifact has a working Bluetooth backend other than Win32: Qt builds the WinRT one
solely for MSVC (`QT_FEATURE_winrt_bt` is 1 there and -1 in every mingw package),
so it is the only artifact that can be tried with the console unpaired. The mingw
Qt 5 build is still the reference for everything else, and is what to reach for if
a Qt 6 result looks strange. See `WINDOWS-QT6-PHASE1.md`.

**Use `windows-exe-only` for iterating.** The full zip is ~62 MB and has failed
mid-transfer; the exe on its own is a few MB compressed and downloads in seconds.
Drop it straight into an existing `C:\QZ\...` install, over the old `.exe`.

It is only a drop-in replacement while the Qt DLL set is unchanged. Any commit
touching the `QT +=` line in `src/qdomyos-zwift.pri`, the Qt version, or the
toolchain needs the full `windows-binary-no-python` instead. QZ logs its build on
every launch, which is how you check:

```
QZ build 6eb7428 Qt 5.15.2 on Windows 11 ...
```

If that Qt version does not match the DLLs in the install, take the full artifact.
The commit on the same line answers "am I actually running the new binary?" - which
used to be answered by grepping ASCII out of the `.exe`.

Prefer **mingw** when comparing against upstream. The upstream nightly publishes a
`windows-binary-no-python.zip` built from master with that same toolchain, so
testing a fork build against it changes one variable - the branch - instead of two.
The msvc2022 artifact is a poor baseline for two reasons at once: a different Qt
major version, and a source tree that is neither this fork nor upstream master.

The `-no-python` variants omit the bundled Python and PaddleOCR payload, which only
serves the Zwift-workout OCR and NordicTrack ADB paths. They are several hundred MB
smaller and fine for everything else.

All Windows nightlies are **debug** builds - CI archives `src/debug/output` - so the
Qt DLLs carry the `d` suffix and the binaries are large. Do not read performance
into them.

## Which jobs run

A dispatch used to fan out to 21 jobs. Everything except Android and Windows is now
gated off with a literal `if: false` carrying the marker comment
`# fork: disabled - only android-build and window-* build here`:

```sh
grep "fork: disabled" .github/workflows/main.yml
```

Left running: `window-build`, `android-build`, and `linux-x86-build`.

`linux-x86-build` is back on as a **tests-only** job. It is the only job in the
workflow that runs the gtest suite, and while it was off every test written had to
be run by hand in a build VM. It publishes no Linux binary and gates nothing, so it
runs beside the Windows build rather than adding its minutes to that critical path.

Disabled here: `ios-build`, the three `raspberry-pi-*` jobs, `android-emulator-test`,
the vendor APK flavours `nordictrack-build` and `fitpro-build`, and:

- `window-msvc2019-build` and `window-msvc2019-aiserver-build`, which carried no
  gate at all and so started three Windows runners on every push and every PR for
  builds this fork does not ship.
- `window-msvc2022-pr-build`, whose first step checks out `ref: qt6`. This fork has
  no `qt6` branch, so it failed on that step on every PR and reported a red X that
  had nothing to do with the PR.

Re-enabling one is a single word - change its `if: false` back.

The nightly `schedule:` trigger is gone. `window-msvc2022-build`,
`peloton-bike-build`, `peloton-bike-plus-build` and `upload_to_release` are all
gated on `github.event_name == 'schedule'` and so are now dormant; they are left
gated that way so restoring the cron restores them.

A push to a feature branch does not double up with its PR run - `on: push` is
already restricted to `master`. A manual `workflow_dispatch` on a branch that also
has a PR open *is* two runs; that is the dispatch, not the push.

A superseded run reports `cancelled`, not `failed`, because of
`concurrency: cancel-in-progress`. Anything polling a run ID must treat `cancelled`
as "find the newer run".

Gating rather than deleting keeps the diff against upstream to one line per job, so
a merge conflicts on a line instead of on a missing block.

## Building locally

A full build takes about two and a half minutes on this machine, against roughly
twenty for a CI round trip, so this is the loop worth using while iterating.

What is installed:

- **MSYS2** with `mingw-w64-x86_64-toolchain` (currently GCC 16.2) and
  `mingw-w64-x86_64-qt5-webview`. The webview package matters: Qt's official mingw
  5.15.2 build ships no WebView.
- **Qt 5.15.2** `win64_mingw81` plus `qtnetworkauth` and `qtcharts`, in `C:\Qt`,
  installed with `aqt` (`pip install aqtinstall`).
- **CMake** and **7-Zip**.
- Submodules - these are real submodules, so no manual cloning:
  ```sh
  git submodule update --init --recursive src/smtpclient tst/googletest src/qthttpserver
  ```
  `android_openssl` and `zwiftplay` are not needed for Windows; the build files do
  not reference `zwiftplay`, and upstream CI does not check either of them out for
  the Windows jobs.

### PATH order is load-bearing

`C:\msys64\mingw64\bin` must come **before** `C:\Qt\5.15.2\mingw81_64\bin`:

```powershell
$env:PATH = "C:\msys64\mingw64\bin;C:\Qt\5.15.2\mingw81_64\bin;" + $env:PATH
```

Qt's `bin` ships its own `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and
`libwinpthread-1.dll`, all dated May 2018 - they are the GCC 8.1 runtime Qt was
built with. Put that directory first and GCC 16's `cc1plus.exe` loads the 2018
`libstdc++`, fails to start, and exits 1 printing **nothing at all**. qmake's
compiler probe in `mkspecs/features/toolchain.prf` sees the non-zero status and
reports:

```
Project ERROR: Cannot run compiler 'g++'. Output:
===================
===================
```

which is the exact message Smart App Control used to produce, for a completely
different reason. `g++ --version` works fine from the same shell either way,
because that path does not need `cc1plus`. To confirm which one you have, run the
probe qmake actually runs:

```
cmd /c "g++ -E C:/Qt/5.15.2/mingw81_64/mkspecs/features/data/macros.cpp"
```

Exit 0 with `QMAKE_GCC_MAJOR_VERSION = 16` at the end means the toolchain is fine.

### qthttpserver, and why the header-repair loop is not optional here

`window-build` calls the forwarding-header repair in `main.yml` a "guard" that
reported *25 scanned, 0 repaired*. That is true on the runner, which has
Strawberry Perl. This machine has only MSYS2's and Git's perl, both of which
report POSIX paths, so `syncqt.pl` decides `<srcbase>` and `<bldbase>` are
different roots and writes every forwarding header as a relative path climbing
between them. Locally the same loop reports **25 scanned, 23 repaired**, and
without it nothing that includes a QtHttpServer header compiles.

```powershell
$env:PATH = "C:\msys64\mingw64\bin;C:\Qt\5.15.2\mingw81_64\bin;" + $env:PATH + ";C:\msys64\usr\bin"
cd src\qthttpserver
cp ..\..\qHttpServerBin\5.15.2\headers\* src\3rdparty\http-parser\
Remove-Item -Recurse -Force examples -ErrorAction SilentlyContinue
Set-Content -Path tests\tests.pro -Value "TEMPLATE = subdirs"
qmake -r
# then the repair loop from .github/workflows/main.yml, verbatim
mingw32-make -j8
mingw32-make install
```

`C:\msys64\usr\bin` goes at the *end* of PATH: syncqt needs a perl, but that
directory also carries an MSYS `make`, `find` and `sort` that should not win.

### The build itself

```powershell
lrelease src/qdomyos-zwift.pri
qmake
mingw32-make debug -j8
```

**`debug`, not the default target.** qmake generates debug and release makefiles
for every subproject; left to itself `make` builds `src` in debug and then tries
to build `tst` in release, which stops at

```
No rule to make target '.../src/release/libqdomyos-zwift.a'
```

Naming `debug` keeps all three subprojects on the same side. It also matches CI,
which archives `src/debug/output`.

Then the tests, which are worth running because `linux-x86-build` is the only CI
job that builds them:

```powershell
tst\debug\qdomyos-zwift-tests.exe
```

### Running what you built

Do **not** run `src\debug\qdomyos-zwift.exe` in place. With `C:\msys64\mingw64\bin`
first on PATH it loads MSYS2's Qt - the build banner says `Qt 5.15.19` instead of
`Qt 5.15.2` - and the QML modules resolve against the wrong prefix, so the engine
fails to load a single component. Deploy it the way CI does, into its own
directory:

```powershell
$env:PATH = "C:\Qt\5.15.2\mingw81_64\bin;C:\msys64\mingw64\bin;" + $env:PATH   # note: reversed
mkdir out; cp src\debug\qdomyos-zwift.exe out; cd out
windeployqt --qmldir ..\ qdomyos-zwift.exe
cp C:\msys64\mingw64\bin\libwinpthread-1.dll,C:\msys64\mingw64\bin\libgcc_s_seh-1.dll,C:\msys64\mingw64\bin\libstdc++-6.dll .
cp ..\windows_openssl\*.* .
```

Qt's `bin` goes first *for running* and last *for building*; the DLLs it shadows
are the ones windeployqt is about to copy anyway. The banner on the first line of
the log is the check: `QZ build <sha> Qt 5.15.2 on Windows ...`.

### The build banner lies if you only re-run qmake

`QZ_GIT_SHA` is baked in by qmake (`src/qdomyos-zwift.pri`), and a top-level
`qmake` regenerates only `Makefile`. The per-subproject ones are created lazily -
`test -e Makefile.qdomyos-zwift-lib || qmake -o ...` - so they keep the old SHA
and the binary claims to be a commit it is not. Delete them first:

```powershell
Get-ChildItem -Path .,src,tst -Filter "Makefile*" -File | Remove-Item -Force
```

The same staleness is why a deleted header can still be listed as a prerequisite
long after it is gone. When a build fails with `No rule to make target` naming a
file that should not exist any more, this is it.

## Building locally with MSVC and Qt 6

The Qt 6 build is a different toolchain end to end, and it is the only one whose
Bluetooth backend is WinRT rather than Win32. What it needs:

- **VS 2022 Build Tools** with the "Desktop development with C++" workload -
  `--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended` is enough,
  no IDE. Installs to `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`.
- **Qt 6.8.2 `win64_msvc2022_64`** in `C:\Qt`, with `qtconnectivity qtpositioning
  qtnetworkauth qtwebsockets qtspeech qtlocation qtmultimedia qtcharts qthttpserver
  qtshadertools qtimageformats qt5compat`. Confirm the backend is real, not the dummy:
  `include/QtBluetooth/6.8.2/QtBluetooth/private/qtbluetooth-config_p.h` must say
  `#define QT_FEATURE_winrt_bt 1`.
- **protobuf from vcpkg**, because `zwift_messages.pb.cc` is compiled only under
  msvc. It must be pinned - see below.
- **python on PATH.** Unlike Qt 5, the Qt 6 build generates its QML resources:
  `src/qdomyos-zwift.pri:377` runs `tools/qt6-qml-imports.py` from inside `qmake`
  itself, so a missing interpreter fails at configure time rather than at build time.

### The script

`tools/build-qt6-win.ps1` does all of the below and checks each precondition before
it can turn into a failure somewhere else - a missing vcpkg becomes `LNK2019` on a
protobuf symbol several minutes in, a missing python becomes `rcc` complaining
about a `.qrc` nothing wrote, and a Qt with the dummy Bluetooth backend builds
perfectly and then cannot see a bike.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-qt6-win.ps1 -Tests
powershell -ExecutionPolicy Bypass -File tools\build-qt6-win.ps1 -Deploy    # runnable tree
powershell -ExecutionPolicy Bypass -File tools\build-qt6-win.ps1 -DeployTo C:\QZ\lite-version
```

`-DeployTo` replaces the `.exe` in an existing deployed tree, which is the fast
iteration loop. It is only valid while the Qt DLL set is unchanged - the build
banner QZ logs on launch is the check.

### What it does, by hand

```powershell
# the MSVC environment has to come from cmd; copy it back into the session
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && set' |
    ForEach-Object { if ($_ -match "^([^=]+)=(.*)$") { Set-Item ("env:" + $matches[1]) $matches[2] } }
$env:PATH = "C:\Qt\6.8.2\msvc2022_64\bin;" + $env:PATH

lrelease C:\projetos\qz\qz\src\qdomyos-zwift.pri

# shadow build, so the mingw tree in src/debug stays intact
mkdir C:\qz-qt6; cd C:\qz-qt6
qmake C:\projetos\qz\qz\qdomyos-zwift.pro "VCPKG=C:\qz-deps\vcpkg\installed\x64-windows"
nmake debug
```

**Pass `VCPKG=`, never a raw `INCLUDEPATH`/`LIBS` pair.** vcpkg ships two builds of
protobuf and abseil, and only `defaults.pri` knows which side of `debug|release`
this build is on; handed the triplet root it picks `debug/lib` or `lib` to match the
CRT. Pointing `-L` straight at `installed/x64-windows/lib` from a debug build links
the release half, and on Qt 6 that is not a link error - it is the silent
`std::string` size mismatch written up at `defaults.pri:20-38`, which surfaces as
gtest aborting on an empty test name. This recipe used to say exactly that, and it
was wrong.

Running the tests needs one more thing, for the same reason CI needs it: a debug
build imports `libprotobufd.dll` and the debug `abseil_dll.dll`, which live in
`debug\bin` and are on nobody's PATH. Without them the exe dies before `main()`
with `0xC0000135`, reported as an exit code that looks like a test failure.

**Pin vcpkg's baseline to `8c2fcacefba009d63672f9d137f192765e632c9f`**, the one CI
uses, by installing in manifest mode rather than `vcpkg install protobuf`. A
current vcpkg gives protobuf 6.x, and the build then dies on

```
zwift_messages.pb.h(15): fatal error C1189: #error: "This file was generated by a newer version of protoc which is"
```

which reads backwards: the file was generated by protoc **4.25.1** and reads
`PROTOBUF_VERSION` out of `google/protobuf/port_def.inc`. Protobuf 6.x moved that
macro to `runtime_version.h`, so the guard compares against an undefined macro,
sees 0, and concludes the headers are too old.

## Testing the binary

**Close QZ before running the test suite.** A running instance answers the mDNS
queries the `DirconDiscovery` tests send, because it is a real responder on the
same multicast group and it gets there first. All eight fail, and they fail
looking like a DIRCON regression rather than a port conflict:

```
TestDirconDiscovery.h(267): Expected equality of these values:
  kBikePort          Which is: 47820   <- what the fixture advertises
  srvs.first().port  Which is: 36866   <- what actually answered
```

A second symptom of the same thing, if the running instance was launched from the
deploy directory: `windeployqt` cannot overwrite a locked `qdomyos-zwift.exe`, so
`-Deploy` fails after a build that was perfectly good.

`tools/build-qt6-win.ps1 -Tests` does not check for this. It is worth a glance at
Task Manager first, or `Get-Process qdomyos-zwift`.

The bike is Bluetooth LE, and **BLE does not work in a VM** - VirtualBox has no
virtual Bluetooth adapter, and the only route is USB passthrough of a physical
dongle, which needs the Extension Pack (not installed) and is unreliable for BLE
anyway. Whatever builds the binary, it has to be run on the machine holding the
Bluetooth adapter.

Two Windows-specific behaviours to keep in mind when a bike connects but no data
arrives:

- ~~`bluetooth::finished` is not connected on Windows.~~ **Fixed 2026-08-24.** It is
  connected on every platform now. `finished()` ends with `startDiscovery()`, so it
  *is* the scan cycle, and excluding Windows meant discovery ran once and stopped for
  good when the agent hit its timeout - which is Qt's 40 s default here, because
  `setLowEnergyDiscoveryTimeout()` is skipped on Windows too.

  It went unnoticed while nothing ever went back to scanning. `bluetooth::rescan()`
  does, and the 21:01 session shows the cost: the rescan started a scan at 21:02:30,
  the agent went silent at 21:03:10, and QZ then displayed "searching" for eight
  minutes while scanning for none of them.

  Original text, worth keeping because it is what a reader was told for months:
  *"the rescan cycle lives in that handler. Discovery is effectively one-shot at
  launch, so the bike must be advertising before QZ starts."* The first half of that
  is no longer true; the second half is now only true of the window before the first
  scan completes.
- **The bike must be paired in Windows Settings first.** Until it is bonded at the
  OS level, the connection succeeds and the services enumerate, but no
  notifications are ever delivered - `characteristicChanged` stays at 0 and every
  tile reads zero. After bonding it jumped straight to 158 on the same build.
- **`applewatch_fakedevice` hijacks the connection.** The `fake_bike` branch in
  `bluetooth::deviceDiscovered` is evaluated *before* the `ftmsbike` branch, so
  with that setting on, the real bike is never reached. Turn it off before
  concluding anything about a Windows connection failure.

- **Windows owns the GATT cache, and nothing invalidates it.** The
  `ATT_ATTRIBUTE_NOT_FOUND` warnings in the log are Windows serving attribute
  handles out of the bond record for characteristics the bike no longer exposes.
  Android's stack invalidates on a Service Changed indication and re-reads;
  `QLowEnergyControllerPrivateWin32` - which is the backend these builds use - does
  not handle Service Changed at all, so a stale cache stays stale.

  The handles seen going stale so far (`2a05`, `fff1`, `fff2`, `d18d2c10-...`) are
  not characteristics QZ uses, so this is currently cosmetic. It does mean the bond
  carries a stale database, though, and **if `2ad2` or `2ad9` ever land in that set
  the only remedy is to remove the device in Windows Settings and pair it again.**
  Reach for that before rebuilding anything.

  The mechanism is narrower than "no Service Changed handling", and worth stating
  exactly, because it decides where a fix can go. Qt asks Windows for the *cached*
  enumeration every time: `qlowenergycontroller_win.cpp` passed
  `BLUETOOTH_GATT_FLAG_NONE` to `BluetoothGATTGetServices`,
  `BluetoothGATTGetCharacteristics` and `BluetoothGATTGetDescriptors`, which lets
  Windows serve whatever it last stored. Nothing ever asks for the device.

  **This is not fixed by changing backend or Qt version.** Qt 5.15's WinRT backend
  uses the plain `GetGattServicesAsync` / `GetCharacteristicsAsync` /
  `GetDescriptorsAsync` overloads, which default to `BluetoothCacheMode_Cached`,
  and never calls a `*WithCacheModeAsync` variant for enumeration - and Qt 6.8 is
  identical in this respect. Only the *value* reads are explicitly `Uncached`, and
  on the Win32 backend not even those. The defect follows you everywhere.

  `qt-patches/windows/5.15.2/qlowenergycontroller_win.cpp` now passes
  `BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE` on all three enumeration calls,
  falling back to the cache once if the forced read fails so that discovery is
  never made *more* fragile than it was. It is built into the shipped DLL - see
  below - and announces itself once per run so a log can prove which module is
  loaded:

  ```
  qt.bt.windows: QZ patched Qt5Bluetooth: GATT enumeration forced from device
  ```

  If that line is absent from a Windows log, the build is running stock Qt and the
  unpair-and-re-pair remedy above is still the only cure.

## The patched Qt Bluetooth module

QZ patches Qt's Bluetooth module. Those patches used to ship as committed DLLs -
30 MB of mingw binary with no `.prl`, no build script, and no recorded procedure
for reproducing it. `window-build` now builds `qtconnectivity` from source the same
way it already builds `qthttpserver`:

1. `qt/qtconnectivity` is checked out at **`v5.15.2`** - the tag must match the Qt
   the job installs, because a mismatch here is an ABI mismatch.
2. The three patched sources in `qt-patches/windows/5.15.2/` are copied over the
   pristine tree.
3. `qmake -r -- -feature-native-win32-bluetooth` selects the Win32 backend
   deliberately. It is the opt-in: `winrt_bt` is conditioned on
   `config.win32 && !features.native-win32-bluetooth && tests.winrt_bt`, and
   `native-win32-bluetooth` is `autoDetect: false`, so WinRT is the *default* on any
   win32 build whose compile test passes. That test happens to fail under mingw
   today, which is the only reason Win32 is selected at all. Asking for it
   explicitly stops a future toolchain from silently switching backends.
4. `make install` puts the result in the Qt prefix, so `windeployqt` deploys it
   like any other Qt module.

The build is cached against the hashes of the three patch files, so editing a patch
rebuilds the module and leaving them alone costs nothing.

**The `window-msvc*` jobs no longer have their Bluetooth DLLs.** They were removed
with the mingw one. Re-enabling any of those jobs means wiring the same
source-build there first, pointed at the MSVC toolchain - and note that an MSVC Qt
selects the WinRT backend rather than Win32, so it needs the WinRT patches, not the
Win32 one.

An earlier version of this file blamed `ftmsbike::stateChanged()` for refusing to
subscribe until every service reaches `ServiceDiscovered`. That was wrong: a
Windows debug log from this bike shows `all services discovered!` firing normally.
The gate exists, but it was not what broke here, and the speculative patch written
against it was reverted before it shipped.

What *did* break, later and separately, was the interaction between that gate and
how services get built. `discoverDetails()` resolves synchronously on the Win32
backend, so creating and discovering each service in one loop drove the gate to
completion against a list holding only the services built so far.
`serviceScanDone()` now creates every service object before discovering any of
them, so the gate sees the whole list.

**Do not read anything into the number of `all services discovered!` lines.** This
file used to say one per connection was correct and several meant the list was
still being built. That diagnostic is dead: the line now sits at the top of
`ftmsbike::subscribeToServices()`, which `stateChanged()` calls again on every
service state change once the subscription plan resolves, and the plan is
deliberately idempotent - *"the plan yields only services that are discovered and
not yet subscribed, so a forced pass is idempotent and skips whatever is still
stuck"*. A healthy connection on this bike logs it **twelve** times: a run on
2026-08-14 that reached `control granted and start acknowledged` and streamed 41
`characteristicChanged` logged exactly as many as the run beside it that was
refused every write. Judge a connection by `2ad9` being subscribed, the handshake
reaching `control granted`, and `2ad2` arriving.

Enable the debug log from QZ's settings; it is written to QZ's writable app
directory as `debug-<timestamp>.log`.

## Rouvy on the same PC (DIRCON over mDNS)

QZ advertises `_wahoo-fitness-tnp._tcp.local` on port 36866 and Rouvy browses for
it. Two things had to be true before that worked, and each failed silently:

- **QZ must publish a real A record.** `ProviderPrivate::publish` calls
  `localipaddress::getIP(QHostAddress())` with a *null* argument, which skips the
  subnet-matching block; with only an Android JNI fallback behind it, every
  desktop platform returned a null address and advertised a service that resolved
  to nothing. Fixed by `bestLocalIPv4()` (commit `e390b331`), which enumerates
  interfaces and scores them so real Wi-Fi/Ethernet beats virtual adapters - which
  matters on any machine with VirtualBox's `192.168.56.1` in the list. Verify in
  QZ's log: `ProviderPrivate::publish QHostAddress("192.168.x.y")`, not `("")`.
- **Windows' own mDNS resolver must stay enabled.** Rouvy's
  `DnsZeroConfLib.dll` imports `DnsServiceBrowse` from `DNSAPI.dll`, i.e. the
  Dnscache mDNS client - *not* Apple Bonjour, which is unused (`mdnsNSP.dll` loads
  only as a Winsock namespace provider). Setting
  `HKLM:\SYSTEM\CurrentControlSet\Services\Dnscache\Parameters\EnableMDNS = 0`
  makes the browse fail and **hard-crashes Rouvy**: its failure callback re-enters
  `StopBrowsing` on a handle `StartScan` has not finished building. The
  crash is preceded by `ERROR WindowsBonjourBrowser - Network Service Discovery:
  scan failed`, and `NetworkCheckingManagerOnStatusChanged` re-triggers it on every
  address change, so a DHCP renewal becomes a crash loop.

- **Announcements must leave by the right interface.** qmdnsengine joins the
  multicast group on every interface, so queries are always *received* - but a
  socket bound to the any-address *sends* out only one interface, whichever the
  OS picks for `224.0.0.251`. Every virtual adapter on a typical dev box
  (VirtualBox host-only, Tailscale, Docker, Hyper-V) tends to outrank Wi-Fi on
  interface metric, so the answers go somewhere nobody is listening and the
  failure is completely silent. `Server::sendMessage` /
  `sendMessageToAll` now call `ServerPrivate::writeToAllInterfaces()`, which sets
  `setMulticastInterface()` and sends one copy per usable interface.

To see what the OS would pick, connect a UDP socket to the group and read back
the local address it chose:

```powershell
$u = New-Object System.Net.Sockets.UdpClient
$u.Client.Bind((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any,0)))
$u.Connect((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Parse('224.0.0.251'),5353)))
$u.Client.LocalEndPoint.Address    # the interface announcements would go out
```

Changing `Set-NetIPInterface -InterfaceMetric` does *not* reliably move this:
Windows caches a best-interface for the group and kept using a VirtualBox adapter
demoted to metric 9000. Disabling the adapter does take effect immediately.
`netsh interface ip show joins` confirms group membership per interface, which is
a separate question from egress and will look healthy either way.

There is no port conflict between QZ and Dnscache: qmdnsengine binds 5353 with
`ShareAddress` (falling back to `ReuseAddressHint`), so the two coexist.
Do not try to make QZ the sole owner of 5353 - Rouvy's client *is* the Windows
resolver.

Rouvy's logs are worth reading directly:
`%USERPROFILE%\AppData\LocalLow\VirtualTraining\ROUVY\rouvy.log` and crash dumps
in `%LOCALAPPDATA%\Temp\VirtualTraining\ROUVY\Crashes\*\Player.log`.

`rouvy_compatibility` in QZ's settings shortens the mDNS rebroadcast interval from
30 minutes to 5 seconds (`src/qmdnsengine/src/src/hostname.cpp`), which is what
makes Rouvy notice QZ within a reasonable time.
