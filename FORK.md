# How this fork differs from upstream

This is a fork of [cagnulein/qdomyos-zwift](https://github.com/cagnulein/qdomyos-zwift)
(QZ), kept for one rider, one trainer and one PC. Everything below exists because it was
needed for that setup, not because it is generally better.

QZ itself is the work of **[Roberto Viola (cagnulein)](https://github.com/cagnulein)** and
its contributors — the protocol work, the device support, the training-app integrations,
all of it. This page lists only the delta. If QZ is worth something to you, support him:
[Patreon](https://www.patreon.com/cagnulein) ·
[Buy Me a Coffee](https://www.buymeacoffee.com/cagnulein) ·
[Google Play](https://play.google.com/store/apps/details?id=org.cagnulen.qdomyoszwift) ·
[App Store](https://apps.apple.com/app/id1543684531). This fork takes no money and asks
for none.

Upstream is the project. If you are looking for QZ, go there first.

## Read this before downloading

**Device support has been cut from 132 drivers to 16.** If your machine is not in the list
below, this build cannot talk to it and never will — that is the point of the fork, not a
bug in it. Use upstream instead.

What is left: `ftmsbike` (the FTMS trainer this fork is built around), `cscbike` and
`stagesbike` (the generic BLE cadence sensor and power meter, read beside the trainer —
neither can be the machine any more), `heartratebelt`, `coresensor`, `dircon`, plus the
Elite accessories (`eliteariafan`, `eliterizer`, `elitesquarecontroller`,
`elitesterzosmart`), `fitmetria_fanfit`, `wahookickrheadwind`, `sramAXSController`,
`cycplusbc2controller`, `thinkridercontroller`, and Zwift Play / Zwift Click. The rule was
that a driver goes if it competes with the trainer or belongs to a different sport;
everything that hangs off the rider's own bike — fans, shifters, steering, a body sensor —
stays.

**Only Windows and Android are shipped.** The releases carry a Windows (Qt 6) zip and an
Android APK, nothing else. iOS, macOS, NordicTrack/iFIT, FitPro and Peloton builds are
disabled outright. The Raspberry Pi and Linux x86 jobs do still build in CI — nothing is
published from them, and they are kept because compiling the Linux/BlueZ half is the only
thing that proves it is dormant rather than quietly dead.

**The APK is debug-signed** and the Windows build is unsigned.

## What changed

### The app was stripped to a bridge, and given a new front end

QZ here does one job: carry data and control between the FTMS trainer and the four training
apps that matter — Rouvy, Zwift, Kinomap, MyWhoosh. Everything that is not that job has been
deleted rather than disabled, which is also why there is no way back to upstream (see
[Versioning](#versioning)).

- **Recording is gone.** No FIT files, no history, no charts, no e-mail report, no Strava or
  Garmin upload. The training apps record the ride; QZ does not keep a second copy.
- **Training programs, workout media, TTS and the remote-control web UI** are gone with it,
  along with the machine types this fork cannot have — treadmills, rowers, ellipticals.
- **The UI is new.** `homeform`'s tile system and its 400-odd tiles are deleted; there are
  three screens (Ride, Setup, Settings) built around the gear numeral, on a palette chosen
  for reading mid-ride at arm's length. See
  [docs/fork/UI-INSTRUMENT-CLUSTER.md](docs/fork/UI-INSTRUMENT-CLUSTER.md).
- **Settings went from 1,010 keys to 188.** 621 of the ones removed were named by nothing at
  all — defaults the app wrote at startup and never read again.
- **It speaks Portuguese.** The UI is translated to Brazilian Portuguese, picked in
  Settings → Display → Language and applied without a restart. Upstream's other 29
  catalogues are deleted: every one of them translated `homeform`, so they shipped and
  displayed nothing. See [src/translations/README.md](src/translations/README.md).
- **No console window.** Upstream links the Windows binary as a console subsystem app, so
  a black `cmd` window opens beside it and sits there for the whole ride. Nothing is read
  from it — every line goes to `debug-<timestamp>.log` — so `CONFIG += console` is gone.

### Windows Bluetooth moved to WinRT

Upstream's Windows build uses Qt 5's WinRT-era Bluetooth path, which enumerates GATT from
Windows' bond cache. That means the trainer has to be paired in Windows Settings first,
and a stale bond silently breaks every write.

This fork runs Windows on **Qt 6.8.2 / MSVC 2022** (still qmake, not CMake) with the
WinRT backend, and enumerates GATT **from the device** rather than from the bond cache.
The result is that the trainer works **unpaired** — no Windows pairing step at all. When
the OS does refuse writes against a lapsed bond, QZ now says so and drops the pairing
instead of failing silently.

Android stays on Qt 5. The QML sources are written for Qt 5 and the Qt 6 variants are
generated at build time, so one tree serves both.

### DIRCON and mDNS discovery

Several bugs here made training apps fail to find QZ, or find it and refuse to connect:

- The DIRCON endpoint now comes up **at launch** with process lifetime, instead of
  appearing only when a bike connects and dying with it. An app holding a cached
  discovery record always finds someone listening.
- mDNS announcements go out on **every interface**, not just whichever one the OS picks
  for the multicast group — a virtual adapter (VirtualBox, Hyper-V, Docker) used to
  swallow them.
- QZ answers queries arriving over **loopback**, so an app querying `::1` on the same
  machine gets a reply where it asked.
- The advertised service type is **fully qualified**, which is why Rouvy no longer has to
  be started before QZ to discover the trainer.
- QZ stopped **renaming its own service every five seconds** by mistaking its own
  announcement for a competing claim.
- The SRV target is a legal hostname — spaces are no longer smuggled in from the device
  name via Bonjour's `\032` escaping.
- A goodbye is sent on quit rather than leaving a stale record behind.
- The SRV hostname is the instance name with spaces hyphenated, which is the form
  MyWhoosh's resolver insists on.
- Only an A record is advertised, because the listener binds `AnyIPv4` — an AAAA record
  sent a client to an address nothing was listening on, where it sat in `SYN_SENT`.
- The FTMS feature bitmask (`0x2ACC`) declares **power measurement**. Without bit 14 a
  client can connect, decide the trainer measures nothing useful, and never read
  `0x2AD2`.
- No unsolicited zero-filled `0x2AD2` frame is pushed at a new client. A client that
  latches the reported quantities from the flags of the *first* Indoor Bike Data frame
  reads all-zero flags as "measures nothing" and drops the characteristic.

**MyWhoosh and QZ do share one PC.** The long-standing claim that MyWhoosh refuses a
DIRCON device on its own IP — [issue #3314](https://github.com/cagnulein/qdomyos-zwift/issues/3314) —
does not match its code. It was four stacked bugs, each hidden by the one before it: the
last four items above. Confirmed 2026-08-17, with Rouvy connected at the same time. On
Windows MyWhoosh also needs Apple's Bonjour service running; it ships the installer inside
its own package.

### Gears

- **Gamepad shifting.** An Xbox controller shifts gears while the training app keeps
  focus and owns the screen, with configurable buttons, hold-to-repeat, and an ERG toggle.
- **The gear table describes a cassette.** Upstream hard-codes one gear to five resistance
  levels, which on a 32-level trainer leaves six usable gears before the ceiling. Setting
  a **neutral gear** turns the custom gear table's rows into the resistance level each
  gear should reach, and the row count becomes the number of gears — so a 15-row table is
  a 15-speed. The neutral gear rides exactly what the training app asked for.
- Two gear bugs went with it: the gear was being applied **twice** on the app-driven path
  (`changeResistance()` already folds it in), and the simulated-grade path measured gears
  from zero rather than from neutral, so an ordinary gear added a permanent climb.

### On-screen display

QZ publishes gear, ERG state and resistance to the **RTSS overlay** itself, so the numbers
are visible on top of a full-screen training app. There are also standalone AutoHotkey
tools under `tools/` for the tablet + Windows topology, including Xbox → MyWhoosh
shifting.

### Behaviour

- The bike reports the effort **the rider actually made**, rather than the requested value.
  This console publishes the level it was *commanded to*, and the magnets take 6–9 seconds
  to get there, so the requested value is a number the legs never produced — measurements in
  [docs/fork/MEASURED-BIKE.md](docs/fork/MEASURED-BIKE.md).
- **A resistance slew limiter** keeps QZ from asking for more than the actuator delivers
  (`resistance_slew_up`/`_down`, both 0 = off), which is what makes the reported watt true.
- **The ERG level selector was rebuilt.** Interpolating unlearned levels instead of copying a
  neighbour's whole row, extrapolating below the learned cadence range, and taking the
  nearest level rather than the one below took the mean error from −13.2 W to +2.7 W.
- QZ waits for the trainer to grant control before starting a session, and keeps looking
  for the bike, restarting it whenever it reconnects.
- **The screen says when the bike is gone.** The status chip distinguishes searching,
  connecting, discovering, live, stale, lost and gave up; readings that stopped being current
  are dimmed and carry their age rather than standing for ever. On Windows the disconnect
  never arrives from the OS at all, so a link that stops delivering for 10 seconds is hung up
  on from this side to produce one.
- Reconnect hygiene: exponential backoff to a 5-minute ceiling, service objects freed, and
  the likely cause named in the log instead of a bare failure.
- The desktop licence check is gated behind `LICENSE`, as Android's already was.
- The automatic ERG detector is parked, with an account of why in the source.

### Build and CI

- CI ships **Android and Windows only**. iOS, macOS, NordicTrack/iFIT, FitPro, Peloton and
  the second Windows/MSVC 2019 pair are all disabled; the Raspberry Pi and Linux x86 jobs
  build but publish nothing, and exist to keep the dormant Linux/BlueZ half compiling.
- A `window-qt6-build` job covers the MSVC 2022 / Qt 6.8.2 route.
- Two checks the strip added and now depends on: `settings-integrity`, which refuses a key
  that is declared and not catalogued or bound from QML, and `qml-syntax`.
- The nightly `schedule:` trigger is dropped — it burnt runner minutes on release
  plumbing this fork does not publish. The weekly `update-translations` workflow went with
  it: it opened PRs against a `master` branch this fork does not have, for 29 catalogues it
  no longer ships.
- Assorted build repairs: MSVC CRT matching (`_ITERATOR_DEBUG_LEVEL`), the app link no
  longer clobbers the static library, qthttpserver builds without a native perl, and
  `aqtinstall` is pinned. SmtpClient used to be pinned here too; the strip's recording
  phase removed the e-mail report, so the submodule and its six CI checkout steps are
  gone.

## Why, in detail

The working notes behind these changes — measurements, dead ends, and the things that
turned out not to be the cause — are in [docs/fork/](docs/fork/).

## Versioning

Releases are tagged `v<upstream base>-qz.<n>` — for example `v2.21.6-qz.1`, meaning the
first release of this fork built from upstream 2.21.6.

**The base does not move.** This fork deletes upstream code outright rather than carrying
patches on top of it (see [docs/fork/STRIP-SPEC.md](docs/fork/STRIP-SPEC.md)), so rebasing
stopped being possible. `2.21.6` is a record of where the tree came from, not a number that
will be bumped; the `-qz.<n>` counter keeps climbing and never resets. Upstream fixes worth
having arrive by reading the diff and reimplementing them here by hand.

`v2.21.6-qz.2` is the last release with upstream's shape intact — the fallback if a deletion
turns out to have taken something load-bearing with it.

The same string lives in [`src/qzforkversion.h`](src/qzforkversion.h) and is written to
the top of every log:

```
QZ fork release 2.21.6-qz.2
QZ build <sha> Qt <version> on <os>
```

The release workflow refuses to publish if the tag and that header disagree.

## Contributing

Changes here are **not** sent upstream. If something in this fork is useful to QZ proper,
take it and open your own pull request against
[cagnulein/qdomyos-zwift](https://github.com/cagnulein/qdomyos-zwift).

## Licence

GPLv3, unchanged from upstream. Copyright for the original work remains with the QZ
authors; this fork's modifications are offered under the same terms, and the complete
corresponding source is this repository.
