# The instrument cluster — UI refactor

The visual direction the stripped bridge is built in, and the design decisions behind the two
things the ride screen could not previously say: **whether the trainer is actually there right
now**, and **how much battery it has left**.

Chosen 2026-08-24 out of four directions (against a lime "race" treatment, a refined
slate/Material evolution, and a light "daylight ink" one). Built the same day.

STRIP-SPEC.md section 9 still governs *what is on each screen*. This governs how it looks and
what it is allowed to claim.

---

## 1. Palette and type

The old tree took `Material.theme: Dark` with a stock cyan accent, so every colour decision was
Qt's. These are ours, and they carry one rule that is easy to break and silent when broken:

**The accent is not a semantic.** Amber is the instrument colour — the gear numeral, the active
tab, ERG when on, focus. Green, cyan and red mean exactly one thing each. A red dot is therefore
never decoration, and a healthy battery does not borrow the green.

| Token | Value | Meaning |
| --- | --- | --- |
| `ground` | `#0C0B0A` | warm near-black, the window |
| `surface` | `#141210` | chips, buttons. One elevation, no shadows |
| `line` / `lineSoft` | `#26221C` / `#1B1814` | every border is 1 px of these |
| `ink` / `muted` / `dim` / `ghost` | `#F0EBE1` / `#A39C8E` / `#6B6558` / `#4A443A` | the text ramp |
| `accent` | `#F2A33C` | **instrument, not state** |
| `live` | `#63C88A` | connected and carrying data *now* |
| `work` | `#57B6D6` | scanning, connecting, discovering, retrying |
| `fault` | `#E5563C` | was live, is not, and should be |

All of it lives in `src/ui/Theme.qml`, instantiated once in `Main.qml` and reached as
`window.theme` — the same way `window.unit` already was. A QML singleton would need a `qmldir`
and the qrc here is flat with no module.

### Type, and an honest compromise

The direction was drawn in Barlow Condensed (numerals), Barlow (UI) and IBM Plex Mono (data).
**None of the three ships with Windows or Android.** Naming them would mean a silent fallback to
whatever the platform picked, so `Theme.qml` names the nearest faces that are actually present —
Bahnschrift / Segoe UI / Consolas on Windows, the Roboto family on Android.

Bundling the real faces as qrc fonts behind a `FontLoader` is the follow-up. It is a three-line
change to `Theme.qml` and nothing else depends on the family names.

### The grid was already in the code

`Main.qml` computes `unit = max(12, min(w,h)/40)`. On both target sizes — a 480×800 desktop
window and a 400-wide phone — that resolves to **12 px**, so every measurement is a whole
multiple of it. The two dimensions section 9.7 fixed survive the repaint unchanged: an 84 px
(7 u) thumb target on the gear buttons, and a 9-unit gear numeral.

---

## 2. Ride-data chips

The ride line was one concatenated `Label`:

```qml
text: Math.round(rideState.power) + " W · " + Math.round(rideState.cadence) + " rpm · " ...
```

which meant four values shared one colour, one size and **one fate**. The fate is the point. Four
`MetricChip`s can each mark themselves, and TODO.md is explicit that holding a number with no
marker is the only option that is definitely wrong.

Four value states:

- **Current** — solid border, ink value.
- **Stale** — sunk fill, dimmed value, amber key, and the age in seconds. The last reading is
  still there for anyone who wants it; it is no longer *presented* as current.
- **No source** — `––`, not `0`. A coasting rider really does produce 0 W, so zero cannot double
  as "nothing has arrived".
- **Not fitted** — no strap paired. Sunk, quiet, and deliberately *not* the stale treatment: this
  is a bike without a sensor, not a reading that stopped being true.

One small deletion: the line used to end `+ " ♥"`, a dingbat that renders differently on Windows
and Android and carries no more meaning than the letters. The chip says `HR` and `bpm`.

---

## 3. Connection

This is the part that was not cosmetic, and the reason the refactor happened at all.

### What was wrong

`RideState::trainerConnected()` was `currentBike() != nullptr`, and `bluetooth` never clears the
device on a clean disconnect — all fourteen `disconnected()` connections in `bluetooth.cpp` are
commented out. `appConnected()` was built on `lastFTMSFrameReceived != 0`, and neither timestamp
is ever set back to zero.

Both answered *has this ever connected* while reading as *is this connected now*. A pill cannot
be designed out of a boolean that never goes back down, so the design starts from the states.

### The states

`LinkStatus` (`src/devices/linkstatus.h`) is one snapshot, read rather than signalled, because
the UI already polls at 1 Hz and every field is derived from state the driver keeps anyway. Taken
as a struct so a redraw cannot straddle a change — a phase of `Lost` next to an attempt count
from before the disconnect is worse than either alone.

```
                    ┌─────────┐
                    │  STALE  │  no frame for 5 s / frame resumes
                    └────┬────┘
                         │
IDLE → SCANNING → CONNECTING → DISCOVERING → LIVE
          ↑            ↑                       │
          │            │ attempt               │ disconnect
          │      ┌─────┴────┐  backoff   ┌─────┴──┐
       GAVE UP ← │ RETRYING │ ←──────────│  LOST  │
        after    └──────────┘ 1·2·4·8·16 └────────┘
        5 min                  ·30 s
```

Three visual registers answer the question TODO.md asks the chip to answer:

| | never connected | connected | was, and is not |
| --- | --- | --- | --- |
| dot | hollow ring, `ghost` | filled `live` | filled `fault` |
| bar | none | none | countdown to next attempt |

### Three rules the states obey

1. **Never-connected is grey, not red.** Before a ride starts, no training app is the normal
   condition. Red is for something that *was* working.
2. **A training app leaving is grey too.** Quitting Zwift is how rides end. It degrades to a
   past-tense chip that keeps the timestamp, not an alarm.
3. **Only the trainer goes red**, and it is the only thing on screen with a countdown and a
   manual override.

A consequence worth stating because it looks like a bug: **the training-app chip stays green
while the trainer is gone.** That is the truth. When the bike drops, QZ keeps pushing its own
timer's values over DIRCON (VIRTUAL-BIKE.md, phase 3), so the app really is still connected and
really is still receiving. Colouring it red would be a guess.

### The retry ceiling — 5 minutes, timed, not counted

Settled 2026-08-24. `ftmsbike` gives up **five minutes after the disconnect**.

Timed rather than counted, and the existing backoff forces that choice.
`RECONNECT_INITIAL_MS` is 1 000 and `RECONNECT_MAX_MS` caps the doubling at 30 000:

| Try | 1 | 2 | 3 | 4 | 5 | 6 | 7 | … | 14 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Wait | 1 s | 2 s | 4 s | 8 s | 16 s | 30 s | 30 s | 30 s | 30 s |
| Elapsed | 1 s | 3 s | 7 s | 15 s | 31 s | 61 s | 91 s | … | **301 s** |

So five minutes is roughly fourteen attempts, **eight of them identical 30-second waits**. "Try
11" tells a rider nothing about how much patience is left; elapsed time does. Past the first
minute the chip therefore leads with how long the bike has been gone, and the giving-up chip
reports both.

Three decisions attached to it:

- **Retry now resets the clock as well as the backoff.** Someone who has just walked back from
  power-cycling the trainer must not inherit a 30-second wait and a ceiling twenty seconds away.
- **Giving up stops the timer and keeps the controller.** Tearing it down would mean deciding who
  deletes the bike object, and `bluetooth` has no such path — that is its own TODO entry. The
  retry lambda already guards on `m_control`, so stopping the timer is the whole of giving up.
- **Giving up is not silent.** It posts a `QzNotify` toast *and* writes `TRAINER LOST` into the
  RTSS overlay. Per section 9.7 the Windows ride screen is admittedly rarely looked at, because
  the training app owns the screen — which is exactly the moment this fires.

### Discovery progress has no denominator

`LinkStatus::servicesFound` counts `serviceDiscovered()` calls, and there is deliberately no
"of 4". How many services a bike has is not known until discovery finishes, so a denominator
would be a number invented for the sake of a progress bar. The count alone still distinguishes a
discovery that is crawling from one that has stopped — which is the Windows stall in TODO.md,
where four services arrived five seconds apart and the screen said nothing.

---

## 4. Battery

`ftmsbike` has read characteristic `0x2A19` all along. Until this refactor the only thing it did
with the value was fire a three-second toast **on change** — so the number was visible for three
seconds, at a moment nobody chose, and then gone.

It now rides on the trainer chip for as long as the bike is connected, and the toast is deleted.

| Level | Treatment |
| --- | --- |
| > 20 % | neutral `muted` fill. Green means "live"; a healthy battery does not borrow it |
| ≤ 20 % | amber, and the percentage promotes to accent weight |
| ≤ 10 % | red. A flat battery is one of the ways the trainer disappears |
| not reported | the glyph stays, struck through. Some bikes never expose `0x2A19`, and the chip must not silently change shape |

No estimated time remaining. QZ has no discharge model for this trainer, and inventing one would
be a number a rider could plan around and be wrong about.

---

## 5. Gamepad

`gamepadcontroller` already polled XInput, held a sixteen-entry button table, parsed `"rt,lt"`
into a mask and supported two buttons per action. `buttonNames()` even carries a comment saying
it returns the names *"in a stable order for the settings UI"* — and that UI was never built. The
only way to rebind was to edit `gamepad_gear_up` by hand.

`GamepadScreen.qml` does not ask the rider to type `rt`. Sixteen names in a list assumes they
know which physical trigger their pad calls that, on a pad that may be mounted upside down on the
bars. **Pressing it removes the question.**

Capture semantics, each chosen against a specific way it could annoy:

- Buttons **already held** when capture opens are ignored until released. A thumb resting on a
  trigger would otherwise bind that trigger the instant the screen opened.
- The binding commits **on release**, so a mis-press can be rolled off before it lands.
- A button drives **exactly one** action. Binding it somewhere new takes it away from wherever it
  was, rather than firing two actions off one press.
- Capture runs **even when gamepad shifting is switched off**, because that is the state a rider
  is in while setting the pad up for the first time. It also forces the fast poll rate, since the
  controller idles at 1 Hz when disabled and that does not feel like listening.
- Clearing the last binding **disables the action**. That is what an empty setting has always
  meant to `buttonMask()`; the screen only makes it reachable.

`gamepadcontroller` is now built on every platform, not just Windows. It already reported
`available() == false` where XInput is absent, and the screen needs an object to ask — a screen
that cannot say "not supported here" would have to pretend instead, which is the failure section
3.2.1 exists to prevent.

### 5.1 Three backends, because XInput is not the whole of gamepads

XInput answers for Xbox pads and for third-party pads **in their X-input mode**. A pad that has no
X-input mode at all is invisible to it however it is connected — an 8BitDo Micro offers Switch,
D-input and keyboard modes and nothing else, so "not detected" was the correct behaviour of the
wrong assumption. `gamepadcontroller` now asks three backends in order:

| Backend | Where | Reads |
|---|---|---|
| XInput | Windows | Xbox pads, and any pad in X-input mode. Asked first: it is the one that knows an Xbox pad's own labels. |
| `gamepadhid` | Windows | Everything else Windows enumerates as a HID gamepad — an 8BitDo in D-input, a DualSense, a Switch Pro. |
| `gamepadandroid` | Android | Pad key and motion events, forwarded by `CustomQtActivity`. |

`gamepadhid` finds the pad through the raw input device list (usage page 1, usage 4 or 5), opens it
with `CreateFile` and reads its input reports with **overlapped I/O**, so a poll never blocks on a
pad sitting still. Reports are decoded with the HID parser rather than by hand, so a pad that lays
its buttons out differently still comes out right. `hid.dll` is resolved at runtime for the same
reason `xinput1_4.dll` is: a missing DLL should cost a log line, not a startup dialog.

Two honest limits, both stated in the screen rather than papered over:

- **HID button names are positional.** HID reports carry numbers, not labels, and no two pads
  number them the same way, so button 1 becomes `a`, button 2 `b`, and so on. It does not matter in
  practice because the screen binds by pressing — but the footer says to press the button you want
  rather than trust the label, instead of implying the diagram is the rider's pad.
- **Android gives input to the app on screen.** On Windows the pad is read from the device, which is
  the whole point: shifting works while the training app owns the screen. Android has no such route,
  so shifting from the pad works while QZ is in front and not while the training app is. The footer
  says so on Android and nowhere else.

### 5.2 The volume keys, which is how Android shifts under the training app

One Android input does survive losing focus. The system handles the volume keys itself and
broadcasts `VOLUME_CHANGED_ACTION` to every registered receiver, whoever is in front — so a volume
change reaches QZ mid-ride and a button press does not. `MediaButtonReceiver.java` even parks the
media volume back at 7 after each change, which is what makes the shifting endless rather than
running out after a few gears.

**The Java side survived the strip and the native side did not.** `MediaButtonReceiver.java` and the
`volume_change_gears` setting were both still in the tree, but nothing implemented
`nativeOnMediaButtonEvent`, nothing ever called `registerReceiver`, and the setting only set
`QT_ANDROID_VOLUME_KEYS` — which routes the keys to a *focused* QZ and so misses the entire point of
the feature. It went with `homeform`. `volumekeys` is the missing half: it implements the callback,
registers and unregisters the receiver as the setting changes, honours `gears_volume_debouncing`, and
hops to the Qt thread before touching `RideState`, because the broadcast arrives on Android's.

It is also how a pad shifts under the training app: a pad with a keyboard mode — an 8BitDo Micro in
K mode — can be programmed to send volume up and down, and then the same two buttons work while the
rider is looking at Zwift.

---

## 6. The overlay: one producer, as many sinks as the rider wants

The overlay is the only QZ surface a rider sees while the training app owns the screen, which is
why it earns a section of its own rather than a line in the settings list.

It used to be one method on `RideState` that composed the lines and wrote them into RTSS in the
same breath. That was fine while RTSS was the only sink and stopped being fine the moment there
were two, because **"which lines" and "drawn where" are different questions and only the first has
anything to do with the ride.** `QzOsd` is the split: it composes once, off `RideState`'s own
public API, and hands the same string to every sink that is switched on.

| Sink | Setting | Draws | Survives exclusive fullscreen |
|---|---|---|---|
| RivaTuner | `osd_rtss` | inside the training app's own D3D frame | **yes** — this is the whole reason RTSS is worth the dependency |
| Floating window | `osd_window` | a frameless always-on-top `Window` | no — it is an ordinary window, and an exclusive-fullscreen app bypasses the compositor |

The second sink exists for riders without RivaTuner, and the table is the honest version of what
it buys them: everything except the one case RTSS was chosen for. Windowed and borderless training
apps — which is most of them — are fine. Both rows are Windows-only, and the settings group is
hidden elsewhere: RivaTuner does not exist on Android, and a Qt window cannot float over a training
app that is a *separate Android app*, because it lives inside QZ's own activity. An Android
`TYPE_APPLICATION_OVERLAY` would be a third row in this table and one more branch in
`QzOsd::refresh()` — that is the shape's whole point.

Three details worth keeping:

- **Off releases the RTSS slot** rather than only stopping the writes. RTSS redraws the last text
  it was handed for as long as the slot stays claimed, so "stop publishing" would freeze a stale
  gear over the training app instead of removing it.
- **The trainer-lost notice is not one of the switchable lines.** The per-line switches choose
  which *numbers* are worth screen space; that notice is not a number, it is the announcement that
  the numbers have stopped. Declining it means turning the whole overlay off.
- **The floating window is click-through while locked**, so a stray click mid-ride reaches the
  training app rather than QZ, and it does not accept focus. Unlocking it is how it gets dragged,
  and the position is written on release rather than on every frame of the drag.

`QzOsd` is deliberately **not** a member of `RideState`. The ceiling below is full, and this is the
wrong kind of thing to spend one on: the overlay is a view of the ride, not a fact about it. It
reaches QML as its own context property, the way `language` and `gamepad` already do — and taking
the overlay out gave `RideState` a private slot and a member back.

---

## 7. What it cost on the RideState surface

STRIP-SPEC section 9.2 capped `RideState` at ~20 members, and `TestRideState` asserted it. **The
ceiling moved to 22.** It is the only time it has moved, and the argument is recorded next to the
assertion so the next person finds an argued ceiling rather than a moved one.

Five members were added: a trainer state, a training-app state, the battery, the resistance range
and the retry countdown. Every one is a fact about the bridge a rider has to be able to see — not
the tile-rendering plumbing the ceiling exists to keep out.

It cost five rather than ten because of three deliberate economies:

- Two booleans were **replaced** by the two state properties rather than joined by them.
- `appName` was **deleted**. It returned an empty `QString` unconditionally, and the QML branch
  reading it (`appName.length > 0 ? appName : "Training app"`) could never take its first arm.
- **One data-age clock does the work of two.** "How stale is this reading" and "how long has the
  bike been gone" are the same instant, because when the link drops the data stops. Carrying two
  timestamps would only have created a way for them to disagree.

`resistanceLevels` needed no new plumbing at all: `maxResistance()` was already virtual on
`bluetoothdevice` and already overridden by `ftmsbike`.

The states are **strings**, not a `Q_ENUM`. `RideState` reaches QML as a context property, so an
enum would need `qmlRegisterUncreatableType` and a module import — and section 9.8 is explicit
that the Qt 6 import rewriter is the easiest way to break the Android build. `TestRideState` pins
both vocabularies instead, which is the only thing standing between a typo and a state no QML
branch matches.

---

## 8. What is deliberately not here

No charts, no history, no lap counter, no session summary. Section 7 deleted all of that and none
of it comes back through a redesign. The ride screen gained four chips and lost a sentence; it did
not become a dashboard.

No light theme. This is an instrument read in a dim room while the training app owns the screen,
and on Windows the ride screen is barely looked at because RTSS already overlays the gear. One
palette, committed to.

---

## Verified

Built locally against `lite-version`, mingw / Qt 5.15.2, per BUILDING-ON-WINDOWS.md.

- `mingw32-make debug -j8` — clean, exit 0.
- `tst\debug\qdomyos-zwift-tests.exe` — 204 tests, **192 passed, 0 failed**, 12 pre-existing
  skips (device-exclusion cases, unrelated).
- Deployed and launched. **No QML warnings from any screen**, including `GamepadScreen` and
  `PadDiagram` (verified by temporarily defaulting the overlay open).
- All four screens captured and inspected. Two layout defects found this way and fixed: a
  `MouseArea` anchored to fill a `Column` in the tab strip, which silently disables the whole
  Column's layout, and a `Layout.alignment: Qt.AlignHCenter` on the resistance label that did not
  centre where `Layout.fillWidth` + `horizontalAlignment` did.

Screen capture needs `QT_QUICK_BACKEND=software`. GDI `BitBlt` cannot read Qt Quick's hardware
swap chain and returns a blank white client area, which looks exactly like a UI that failed to
render.

## Still open

- ~~The service-discovery watchdog is still armed in `serviceScanDone()`.~~ Fixed 2026-08-24:
  armed at `ConnectedState`, stopped at `DiscoveredState`, re-armed by `serviceScanDone()` for
  the subscription pass. Both halves of that TODO entry are now closed.
- Bundling the real type families (section 1).
- `bluetooth` still never clears the device on a clean disconnect. Nothing here depends on it any
  more — the link phase is read off the surviving bike object — but the fourteen commented-out
  `disconnected()` connections are still there.
- A link that reaches `DiscoveredState` and never sends a first frame **is** torn down, after
  `FIRST_FRAME_GRACE_MS` (15 s). This bullet used to say it was left alone; that was true of the
  watchdog's first draft only, and saying otherwise here is what put the same wrong claim into
  TODO.md when that file was collapsed. What remains open is that a teardown does not *cure* the
  Windows un-bonded case — services enumerate, notifications never arrive, and the remedy is an
  OS-level re-pair QZ cannot perform. It loops visibly to the 5-minute ceiling instead of sitting
  silent, and the chip offers no action short of that.

## What "Retry now" and "Search" actually do (added 2026-08-24)

The chip has one action button and its label changes with the state. That is not cosmetic: the
two labels are two different remedies, and which one is possible is a question about the link
rather than about the UI, so `RideState.retryNow()` decides and the QML does not ask. The
surface therefore stays at 16 properties and 6 invokables.

| State | Label | What happens |
| --- | --- | --- |
| `lost` | Retry now | Resets the backoff and the 5-minute clock, hangs up first if the controller is wedged in `DiscoveredState`, reconnects the same controller. The virtual bike survives, so a training app stays connected. |
| `gaveup` | Search | `bluetooth::rescan()` — deletes the device and scans again. The only remedy when the bike returns on a different address or the driver is wedged, and the only one that costs the training app its connection. |

The escalation is one-way and only at the ceiling, because the cheap remedy is also the
non-destructive one. Reaching for `rescan()` earlier would drop a rider's ride to fix something
a reconnect would have fixed on its own.
