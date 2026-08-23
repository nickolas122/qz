# To do

Things worth doing that are not being done yet, with enough evidence attached that picking one
up does not mean re-deriving why it matters. The same standard as the rest of these notes: what
was measured, where the seam is, and what "done" would look like.

Anything here that turns into work of its own gets a document and leaves a one-line pointer
behind.

---

## ~~The device-name branches in ftmsbike are unreachable from a test~~

**Resolved 2026-08-21.** `applyDeviceProfile()` is that seam - the half of
`deviceDiscovered()` that does not touch the radio - and `simulatedFtmsBike` calls it from its
constructor, so a test is now the bike it says it is: the default harness arrives as a YPBM
with `resistance_lvl_mode` set, ERG unsupported and 32 levels. It went in for
`TestErgSimConflict`, which needs the YPBM profile to reach the continuous-ERG block at all.
`RideScenario`'s `bike` directive is still read by nothing; wiring it up is what is left, and
it is now a small job rather than a blocked one. Original entry below.

**Found 2026-08-18, building Layer B.** `ftmsbike` gates around fifty behaviours on flags set
from the device name — `YPBM`, `DOMYOS`, `FS_YK`, `D500V2` and the rest — and the one that
matters most for this trainer is the three-byte Set Target Resistance it wants
(`ftmsbike.cpp:598`, the level times ten as a 16-bit value) where ordinary FTMS sends two bytes.

Those flags are private members set by `deviceDiscovered()`, which builds a
`QLowEnergyController` and needs a radio. So the harness cannot reach any of those branches, and
the write test asserts that whatever QZ chose is a well-formed FTMS frame rather than that it
chose the right one.

The seam is the same shape as the six already there: lift the name matching out of
`deviceDiscovered()` into something that takes a name and sets the flags, and let a test call it.
`RideScenario` already has a `bike` directive for exactly this, parsed since phase 0 and read by
nothing — this is what would read it.

---

## Windows sometimes never finishes discovering the trainer's services, and the retry is silent

**Found 2026-08-21, the phase 5 hardware session.** The first launch found `YPBM001264` and
connected, then never finished service discovery. All four services arrived, five seconds
apart, each preceded by WinRT's *"Could not await service operation (the operation returned
because the timeout period expired)"*:

```
07:27:35  connectToDevice → ConnectedState → DiscoveringState
07:27:36  serviceDiscovered {00001800-…}      ← 5 s apart, one timeout each
07:27:41  serviceDiscovered {00001801-…}
07:27:46  serviceDiscovered {0000180a-…}
07:27:51  serviceDiscovered {00001826-…}      ← FTMS, and still nothing subscribed
07:27:55  ClosingState → UnconnectedState     ← never reached DiscoveredState
```

Because the controller closed before `DiscoveredState`, no characteristic was ever subscribed,
which is why the trainer never ran its 1-2-3 countdown and no data flowed. The rider saw an app
that had found the bike and then sat there.

QZ did notice: `controllerStateChanged` logged "trying to connect back again" and scheduled a
reconnect 1,000 ms later. Nothing said so on screen, so the app was restarted two seconds after
that — before the retry could land. The next run connected with **zero** timeouts and rode
normally: gears 1–16, resistance 10–26, 38 frames of Indoor Bike Data in 59 seconds.

*Evidence:* `debug-Fri_Aug_21_07_27_24_2026.log` (four timeouts, `Connected → Discovering →
Closing`) against `debug-Fri_Aug_21_07_27_57_2026.log` (none, `Discovering → Discovered`) —
same trainer, 33 seconds apart, same build (`043ba3c`).

The timeouts themselves are probably not ours to fix.
[WINDOWS-BLE-HARDENING.md](WINDOWS-BLE-HARDENING.md) already records that Windows picks its own
connection parameters and that discovery requests get dropped by the OS stack. Two things that
*are* ours, and they are separable:

- **The reconnect is invisible.** Between the drop and the retry the UI says nothing, so a
  recovery that was already in flight looks like a hang — and the natural response, restarting,
  is the one thing that guarantees it cannot finish. Done would be a connection state the rider
  can see: searching, connecting, discovering, connected, reconnecting. This is the same missing
  surface as *QZ does not tell the rider when the bike goes away* below, from the other end — a
  connection that never completed rather than one that ended — and one indicator answers both.
- **Nothing bounds the discovery phase.** `serviceDiscoveryWatchdog`
  (`ftmsbike.cpp:2526`, 10 s) starts in `serviceScanDone()` and so covers the *subscription*
  pass, which is after discovery finishes. This failure was inside Qt's own
  `discoverServices()`, before that timer exists, and the only thing that ended it was the
  controller giving up twenty seconds later. A watchdog armed at `connectToDevice()` and
  stopped on `DiscoveredState` would turn a twenty-second stall into a deliberate retry — and
  the retry path is already written and already works.

Worth knowing before designing either: it is intermittent, it recovered on its own the next
time, and one observation is not a rate.

---

## The write log does not say which characteristic was written

**Found 2026-08-18, building the recorder.** `processWriteQueue()` logs
`" >> " + bytes + " // " + info` (`ftmsbike.cpp:174`). The `WriteRequest` it is logging carries
a `characteristic`, and the line does not print it.

Every `<<` line records its UUID, so a recorded fixture knows exactly which characteristic each
notification arrived on — and then has to store `?` for every write. In practice they are all
the control point, but "in practice" is not something a fixture should encode, and the moment
a write goes somewhere else the recording is quietly wrong rather than visibly incomplete.

One line. `qzlog2ride.py` will pick it up with no change, and recordings made afterwards will be
complete; older ones keep their `?`.

---

## QZ does not tell the rider when the bike goes away

**Reported 2026-08-18, from the fake bike.** Stop the peripheral mid-ride and QZ freezes on the
last frame it received. The tiles keep showing 204 W at 90 rpm for as long as you care to look
at them, and nothing on screen says the bike is gone. A rider cannot tell a working session from
a dead one.

### What actually happens

The transport half is fine, and better than it looks. Since the peripheral started hanging up
properly (`tools/fakebike-android`, `cancelConnection` on stop), the disconnect reaches QZ
immediately and cleanly:

```
14:12:31  last 0x2AD2 notification
14:12:41  BTLE stateChanged InvalidService   (every service, at once)
14:12:41  QLowEnergyController::UnconnectedState
          0 writeCharacteristic timeouts
```

`ftmsbike::controllerStateChanged()` handles `UnconnectedState` correctly: it clears `initDone`,
resets the handshake, and starts `reconnectTimer` with exponential backoff. So QZ *does* try to
get the bike back.

**Nothing above that layer reacts.**

- Every `connect(device, SIGNAL(disconnected()), this, SLOT(restart()))` in
  `src/devices/bluetooth.cpp` is commented out — fourteen of them. `bluetooth` therefore never
  returns to discovery and never tells anyone the device went away.
- `emit disconnected()` fires from the controller *error* handler
  (`ftmsbike.cpp:2694`), not from a clean `UnconnectedState`, so even an uncommented
  connection would miss the ordinary case.
- The metrics are never invalidated. Nothing marks a reading stale, so the last frame stands
  for ever.

### Why it matters beyond the test rig

A trainer that drops mid-ride — flat battery, out of range, another central stealing it — leaves
a rider looking at plausible numbers that stopped being true minutes ago, and a FIT file that
records them. The reconnect backoff may well be working perfectly underneath, and there is no way
to know from the screen.

It is also the one behaviour `dropout.ride` was written for and cannot reach: on the DIRCON side
QZ keeps pushing its own timer's values whatever the bike does
([VIRTUAL-BIKE.md](VIRTUAL-BIKE.md), phase 3), and only a real radio can produce a genuine gap.

### What makes it fixable now

It is reproducible on demand for the first time. Press Stop in the fake bike and the disconnect
happens exactly when you choose, as often as you like, with no trainer and no waiting for a
battery to die. That is the whole argument for having built the peripheral.

### Done looks like

- A disconnect QZ noticed is visible on screen without reading a log.
- Metrics stop being presented as current once they are not.
- The device returns to searching, or says why it is not going to.
- Reconnection, when the bike comes back, does not need the app restarted.

Open question, and a product decision rather than an engineering one: whether the tiles should
clear, grey out, or hold the last value with a marker. Holding a number with no marker is the
only option that is definitely wrong.

### Both status indicators are latches, not state (found 2026-08-23)

**The tiles this entry was written about no longer exist.** Phase 7c-2b deleted them, and the
new tree has something the old one did not: a status pill fed by `RideState.trainerConnected`
and `RideState.appConnected`, which is exactly the place a disconnect should show. So the
"product decision" above is now narrower - the pill is the marker, and the question is only
what it says.

It does not work yet, and the reason is worth writing down because it looks like it works.

**The app half latches on the first frame and never clears.**
`virtualbike::ftmsDeviceConnected()` is:

```cpp
bool ftmsDeviceConnected() { return lastFTMSFrameReceived != 0 || lastDirconFTMSFrameReceived != 0; }
```

Both are timestamps, and **neither is ever set back to 0**. Once Zwift has sent a single FTMS
frame, `appConnected` is true for the life of the process - through the app quitting, the
network dropping, the DIRCON socket closing. `DirconProcessor` logs the disconnection
(`"Disconnection from ..."` appears in every ride log) and nothing upstream reads it.

The fix is already half-built: `whenLastFTMSFrameReceived()` returns the timestamp, so the
question "has a frame arrived recently" is one comparison away. It is a staleness test, not a
new signal.

**The trainer half latches the same way**, for the reason the original entry gives:
`RideState::trainerConnected()` is `currentBike() != nullptr`, and `bluetooth` never clears the
device on a clean `UnconnectedState` because all fourteen `disconnected()` connections are
commented out. The object outlives the radio link.

So the pill currently answers "has this ever connected", and reads identically to "is this
connected now". That is the same failure the tiles had, moved somewhere more prominent.

*How it surfaced:* the 14 `TypeError: Cannot read property 'trainerConnected' of null` warnings
at every QZ exit. Those were a destruction-order bug and are fixed - `RideState` is declared
before the engine now, so the engine is torn down first - but they were the observation that
these are the properties which have to degrade honestly, and today they cannot.

*Done looks like, for the pill:* it distinguishes never-connected from connected from
was-connected-and-is-not, for the trainer and the training app independently, and a rider
glancing at it mid-ride can tell a live session from a dead one without reading a log.

---

## DIRCON on Android needs Wi-Fi, and should not

**Investigated 2026-08-18, on a Samsung A34 (SM-A346M, Android 16) running QZ and Rouvy
together on the one phone.** With Wi-Fi it works. Without it, Rouvy finds nothing, and the
last mile is still open.

Four separate faults were found and fixed getting this far, all in commits from that day:
the missing multicast lock, an mDNS A record with no address in it, a cellular address
being advertised that nothing could reach, and a multicast group that was never joined at
all. Each was real, each is confirmed working from the device's own logs, and none of them
was the whole story.

### What the phone establishes

`ip link` on the device, which is the ground truth here:

| Interface | Flags | Carries multicast |
| --- | --- | --- |
| `lo` | `LOOPBACK,UP,LOWER_UP` | **no** |
| `rmnet2` (mobile data) | `NOARP,UP,LOWER_UP` | **no** |
| `wlan0` | `BROADCAST,MULTICAST,UP,LOWER_UP` | yes |
| `swlan0` (hotspot) | `BROADCAST,MULTICAST,UP,LOWER_UP` | yes |

So a phone with Wi-Fi off and no hotspot has no link at all for mDNS, and no app on either
side can discover anything. That part is not a bug and cannot be fixed in QZ. Turning the
hotspot on gives the device a multicast-capable link of its own making, which is the shape
this ought to work in: no router, no network to join, both apps on the one phone.

### Where it stops

With the hotspot up, everything QZ does is right:

```
mDNS: joined the group through the interface holding QHostAddress("10.30.31.196")
local IPv4 address: 10.30.31.196
ProviderPrivate::publish QHostAddress("10.30.31.196")
LISTEN 0.0.0.0:36866                      (ss -tln)
inet 224.0.0.251 users 4                  (ip maddr show swlan0)
```

Four sockets have joined the group on `swlan0` - QZ among them, and Android's `mdnsd`,
which is what a client using NsdManager goes through. Both ends are on the same link. And
Rouvy still finds nothing.

### The next thing to try

`ServerPrivate::writeToAllInterfaces()` is the suspect, on reading rather than measurement.
It snapshots `socket.multicastInterface()`, walks the interfaces, and restores the snapshot
at the end. On this phone Qt enumerates nothing, so the walk does nothing and the restore
writes back an **invalid** interface - which plausibly clears the `IP_MULTICAST_IF` that
the join fallback had just set. The datagram then falls to `socket.writeDatagram(...)`,
which follows a routing table whose only entry is the `swlan0` link route.

If that is right, QZ can hear the query and cannot be heard answering it, which is exactly
the symptom. Two lines would settle it: skip the restore when the snapshot is invalid, and
re-apply `IP_MULTICAST_IF` before the fallback send.

Unresolved either way: whether Rouvy's discovery runs at all with no connected Wi-Fi
network. `mdnsd` has joined the group on `swlan0`, which is a good sign, but NsdManager
has historically refused to work off Wi-Fi and no amount of QZ-side work would show up
through that.

### Done looks like

- Hotspot on, Wi-Fi off: Rouvy pairs with QZ over DIRCON on the one phone.
- Or the log says plainly that the query never arrived, so the wall is Rouvy's and known.

---

## The wind term of the surface calculation reads the rolling-resistance byte

**Found 2026-08-20, pinning the grade formula.** `CharacteristicWriteProcessor::changeSlope()`
computes two surface corrections from the FTMS Set Indoor Bike Simulation Parameters frame
(`characteristicwriteprocessor.cpp`):

```cpp
const double fCRR = crr / 10000.0;
const double CRR_offset = ((crr - 40) * 0.05) * CRRGain;

const double fCW = cw / 100.0;
const double CW_offset = ((crr - 40) * 0.05) * CWGain;   // crr, not cw
```

`CW_offset` is the wind coefficient's contribution and it never reads `cw`. Both offsets are
therefore the same number scaled by two different gains, and the wind byte the training app
sent is used for nothing but a debug line. `fCRR` and `fCW` are computed and also unused.

It is upstream behaviour and it is inert by default - `CRRGain` and `CWGain` both default to
0, so neither term contributes until a rider opts in. That is why it has survived: the only
people it reaches are the ones who turned surface simulation on, and they calibrated their
gains against the wrong variable without knowing it.

`TestGradeToResistance.TheSurfaceGainsBothReadTheRollingResistanceByte` pins the behaviour as
it stands rather than correcting it, deliberately: changing it changes how every gravel
sector feels for anyone who did turn the gains up, and that is a judgement to make on the
bike.

### Done looks like

- `CW_offset` reads `cw`, or a comment says why it must not.
- Someone with the gains turned on has ridden the change and said whether it feels right.
- The two unused `fCRR`/`fCW` locals are either used or gone.

---

## The toolbar hides under the display cutout, and the insets that would move it are dropped

**Found 2026-08-22 on the A34 (SM-A346M), from the device's own logs.** The header `ToolBar` is
laid out from y=0 while the status bar covers the top 75 px of it, so the drawer button, the
title and the two right-hand buttons are all sliced in half. The bottom sliver stays touchable
and the drawer does open from it, which is why this presents as "the button sometimes does
nothing" rather than as a layout fault: a finger aimed at where the icon looks centred lands
above the window.

`getTopPadding()` in `main.qml:33` is what should prevent it - on Android with API >= 31 it
returns `AndroidStatusBar.height` (`main.qml:44-46`). That property is 0 for the entire life of
the process.

### What the log establishes

Java computes the insets correctly and hands them over 54 ms before Qt exists:

```
17:28:40.083 CustomQtActivity: onApplyWindowInsets - Top:27 Bottom:48 Left:0 Right:0
17:28:40.083 CustomQtActivity: Raw insets   - SystemTop:75 SystemBottom:135 SystemLeft:0 SystemRight:0
17:28:40.083 CustomQtActivity: Cutout insets - Top:75 Bottom:0 Left:0 Right:0
17:28:40.137 QT: qt_process_init() called
```

`AndroidStatusBar::onInsetsChanged()` logs every change it accepts. That line appears **zero**
times in a capture taken with `log_debug` on - the same capture carrying dozens of QML warnings
from the same handler, so nothing was suppressing it. The values never reach C++.

### Two independent reasons it never arrives

`CustomQtActivity.java:119` already knows about the race and gives up on it:

```java
try {
    onInsetsChanged(top, bottom, left, right, ...);
} catch (UnsatisfiedLinkError ignored) {
    // Qt not ready yet; insets will be re-applied once Qt initializes.
}
```

Nothing re-applies them. `onApplyWindowInsets` fires once on this phone and never again, so the
one delivery that mattered is the one that was swallowed. The comment describes a mechanism that
was never built.

Second, and true even if the timing were fixed: `registerQmlType()` installs a singleton factory
returning `new AndroidStatusBar()` (`androidstatusbar.cpp:28`), and the constructor assigns
`m_instance = this` (`androidstatusbar.cpp:14`). Until QML first dereferences the singleton,
`AndroidStatusBar::instance()` is null and the JNI entry point returns having done nothing.
Repairing only the Java side would land the insets in an object no binding is watching.

Worth settling in the same pass: Java divides by `density` and sends dp, while Qt for Android
measures QML in physical pixels. 27 is not 75, so even a delivered value looks like the wrong
unit.

### Why it has not shown up before

The emulator has no cutout. On `qz_test` (API 34, generic profile) the toolbar renders complete -
title, both right-hand buttons, nothing clipped - because a `getTopPadding()` of 0 costs nothing
when the true inset is also 0. Every device with a notch or a punch-hole is affected and no AVD
in use here is, so the whole existing test path is blind to it.

This is upstream behaviour, not a strip regression: the guard was added by `07059849c`
("UnsatisfiedLinkError crash") and nothing in group A-F has touched the inset path since.

### Done looks like

- The header clears the status bar on the A34, with the drawer button whole and touchable along
  its full height.
- `AndroidStatusBar.height` is non-zero by the time the first frame is drawn, and still correct
  after a rotation.
- A device with a cutout and one without both render correctly, since the fix must not add
  padding where there is no inset to clear.
