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

**Device support has been cut from 132 drivers to 18.** If your machine is not in the list
below, this build cannot talk to it and never will — that is the point of the fork, not a
bug in it. Use upstream instead.

What is left: `ftmsbike` (the FTMS trainer this fork is built around), `cscbike`,
`stagesbike`, `smartspin2k`, `strydrunpowersensor`, `heartratebelt`, `coresensor`,
`moxy5sensor`, `dircon`, plus the Elite accessories (`eliteariafan`, `eliterizer`,
`elitesquarecontroller`, `elitesterzosmart`), `fitmetria_fanfit`, `wahookickrheadwind`,
`sramAXSController`, `cycplusbc2controller` and `thinkridercontroller`.

**Only Windows and Android are built.** iOS, macOS, Raspberry Pi, NordicTrack/iFIT,
FitPro and Peloton builds are all disabled here. The releases carry a Windows (Qt 6) zip
and an Android APK, nothing else.

**The APK is debug-signed** and the Windows build is unsigned.

## What changed

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

Known and not fixable here: **MyWhoosh refuses a DIRCON device advertising the same IP as
the machine it runs on**, so QZ and MyWhoosh cannot share one PC. Run QZ on Android or a
second host. This is MyWhoosh's rule, confirmed upstream in
[issue #3314](https://github.com/cagnulein/qdomyos-zwift/issues/3314).

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
- QZ waits for the trainer to grant control before starting a session, and keeps looking
  for the bike, restarting it whenever it reconnects.
- Reconnect hygiene: exponential backoff, service objects freed, and the likely cause named
  in the log instead of a bare failure.
- The desktop licence check is gated behind `LICENSE`, as Android's already was.
- The automatic ERG detector is parked, with an account of why in the source.

### Build and CI

- CI builds **Android and Windows only**; every other platform job is disabled.
- A `window-qt6-build` job covers the MSVC 2022 / Qt 6.8.2 route.
- The nightly `schedule:` trigger is dropped — it burnt runner minutes on release
  plumbing this fork does not publish.
- Assorted build repairs: MSVC CRT matching (`_ITERATOR_DEBUG_LEVEL`), the app link no
  longer clobbers the static library, qthttpserver builds without a native perl,
  `aqtinstall` is pinned, and SmtpClient is pinned to a commit that exists.

## Why, in detail

The working notes behind these changes — measurements, dead ends, and the things that
turned out not to be the cause — are in [docs/fork/](docs/fork/).

## Versioning

Releases are tagged `v<upstream base>-qz.<n>` — for example `v2.21.6-qz.1`, meaning the
first release of this fork built from upstream 2.21.6. Rebasing on a newer upstream moves
the base and resets the counter.

The same string lives in [`src/qzforkversion.h`](src/qzforkversion.h) and is written to
the top of every log:

```
QZ fork release 2.21.6-qz.1
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
