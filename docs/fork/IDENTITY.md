# The name and the mark

What this build calls itself, what it draws to say so, and — the longer half — everything
that deliberately keeps the old name because something other than a rider is reading it.

Chosen 2026-09-07. The palette and type it borrows from are
[UI-INSTRUMENT-CLUSTER.md](UI-INSTRUMENT-CLUSTER.md); this page adds no colours of its own.

---

## 1. The name is QZ-lite

Written **QZ-lite**: `QZ` capitalised, a hyphen, `lite` lowercase. Not *QZ Lite*, not
*QZlite*, not *qz-lite* outside a filename. In the app's header the two halves are set in
different weights — `QZ` bold, `-lite` regular — because they are not equal claims:

> **QZ** is [Roberto Viola's project](https://github.com/cagnulein/qdomyos-zwift). **lite**
> is the only thing this fork adds, and it adds it by subtraction — 132 drivers to 16, two
> platforms out of six, 1,010 settings to 188.

That is also the rule for prose: the fork is *a build of QZ*, never *a product called
QZ-lite that happens to resemble QZ*. [FORK.md](../../FORK.md) is the long version and
outranks this page wherever they disagree.

### Where the name appears

| Surface | Reads |
| --- | --- |
| Window title, taskbar | `QZ-lite` |
| Header, top right of every screen | `QZ` + `-lite`, in `ghost` |
| Android launcher and app info | `QZ-lite` |
| `qdomyos-zwift.exe` → Properties → Details | Product `QZ-lite`, and the description |
| `-h` on the command line | `QZ-lite usage:` |

### Where it deliberately does not

Everything below still says QZ, qdomyos-zwift, or something that is not a name at all.
Each one is read by a machine, or by a person who would be worse off if it changed:

- **`QSettings` organisation and application** (`Roberto Viola` / `qDomyos-Zwift`,
  `main.cpp`). This is the registry key and the ini path every saved setting on every
  machine lives under. Renaming it hands the rider a factory-fresh app and loses the lot.
- **The Android package**, `org.cagnulen.qdomyoszwift`. It is the app's identity to the
  system; changing it is an uninstall and a reinstall, not a rename.
- **The executable and the Qt lib**, `qdomyos-zwift.exe` and `qdomyos-zwift`. CI, the
  build script and the docs name the file, and `android.app.lib_name` in the manifest
  must match the built library exactly. The identity does not live in a filename.
- **The advertised BLE name**, `QZ` (`virtualbike.cpp`). This is what Zwift, Rouvy and
  MyWhoosh show in their pairing lists — *and what they save*. Renaming it silently
  breaks every saved pairing and buys nothing a rider asked for.
- **The DIRCON service names**, `Wahoo KICKR ####` and `ELITE AVANTI #####`. Not names —
  protocol, and matched by string on the other side.
- **The MyWhoosh OpenBikeControl service**, `QDomyos-Zwift OpenBikeControl`. Same: a
  string MyWhoosh looks for.
- **The OSD prefix and the Android notification**, `QZ: TRAINER LOST`, `QZ is Running`.
  Read at a glance mid-effort, over a game. Four more characters buy nothing there.
- **`docs/`, log lines and code comments**, which say QZ throughout and mean the program,
  which is QZ.

If you are about to rename one of these, the question to answer first is *who reads it* —
and if the answer is a training app, an installer, a settings file or a linker, the answer
is no.

---

## 2. The mark is the Q

![The QZ-lite icon](../../icons/qz-lite/qz-lite-512.png)

A single letter — the one the project came from — set in Bahnschrift Bold Condensed, in
`accent` amber `#F2A33C`, on the `ground` `#0C0B0A` with the same 1 px `line` `#26221C`
border every surface in the UI carries. It is the ride screen's gear numeral with a letter
in place of the number, which is the whole idea: the app is an instrument, and its icon is
a reading off one.

Three things it is not, each on purpose:

- **Not upstream's mark.** QZ's own logo is a pink ring around a lime `Z.`; this fork
  shipped a copy of it in `icons.qrc` — unused, since the strip deleted whatever drew it —
  and that copy is now gone. A fork that publishes binaries must not wear the parent
  project's badge.
- **Not a bicycle, a chainring or a lightning bolt.** At 16 px in a system tray they all
  become the same grey smudge. One heavy letterform survives the size; that was the
  selection criterion, not the concept.
- **Not two colours doing semantic work.** Amber is the instrument colour and means
  nothing about state — the rule in UI-INSTRUMENT-CLUSTER.md section 1. Green, cyan and
  red stay out of the icon so that seeing one always means something.

### The master, and how to change it

`icons/qz-lite/qz-lite.svg` is the only place the geometry exists. The letterform was
lifted once out of Bahnschrift as outlines, so nothing downstream — not the build, not a
future contributor on another OS — needs the font installed. Everything else is generated:

```
python tools/make-icons.py            # rewrite every raster from the SVG
python tools/make-icons.py --check    # fail if one of them is stale
```

It needs Pillow and nothing else; the SVG is written with only absolute filled paths so
that the ~60 lines of parsing in that script are all the SVG it has to understand. The
generated files are committed because neither qmake nor the Android packager will run a
generator for us — treat them as build output that happens to be in git, and never edit a
PNG by hand.

### Where it lands

| Platform | File | Notes |
| --- | --- | --- |
| Windows | `src/icons/qz-lite.ico` | 16→256 px, wired as `RC_ICONS` in `src/qdomyos-zwift.pro`. Before this the .exe carried no icon at all. |
| Windows, Qt | `src/icons/icon.png` via `icons.qrc` | `setWindowIcon` in `main.cpp`, so the QML window and the floating OSD both get it |
| Android, launcher | `mipmap-*/ic_launcher*.png` + `mipmap-anydpi-v26/*.xml` | adaptive: flat `ground` background, mark at ⅔ scale inside the 72 dp safe zone |
| Android, shortcuts | `drawable-*/icon.png` | full colour, what `Shortcuts.java` draws |
| Android, status bar | `drawable-*/ic_stat_qzlite.png` | **white silhouette on transparent** — the system tints small icons, so a colour one shows up as a white blob. This is why the notification icon is its own file. |

`icons/Android`, `icons/iOS` and `src/ios/Images.xcassets` still hold upstream's artwork.
They are untouched because this fork ships neither an App Store build nor a Play Store
listing, and there is no branding decision hiding in a file nothing packages.

---

## 3. Type

The wordmark uses `theme.fontDisplay` — the same condensed face as the gear numeral,
which is Bahnschrift on Windows and Roboto Condensed on Android. It is not a separate
brand font, and it inherits the compromise recorded in UI-INSTRUMENT-CLUSTER.md: the
design was drawn in Barlow Condensed, which ships with neither platform, so the nearest
face that is actually installed is named instead. If the real families are ever bundled as
qrc fonts, the wordmark follows them without changing here.
