# To do

Things worth doing that are not being done yet, with enough evidence attached that picking one
up does not mean re-deriving why it matters. The same standard as the rest of these notes: what
was measured, where the seam is, and what "done" would look like.

Anything here that turns into work of its own gets a document and leaves a one-line pointer
behind. Anything that gets fixed is collapsed into [Resolved](#resolved) at the bottom — the
date and what closed it, with the detail wherever the work was written up.

---

## Windows drops service discovery requests

*Retitled 2026-08-24. This was "…and the retry is silent", which it no longer is: both halves
that were ours are fixed, and what is left is the WinRT timeouts themselves — which the entry
has always said are probably not ours. It is kept open because the timeouts are still happening
and nothing here has measured how often.*

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
connection parameters and that discovery requests get dropped by the OS stack. The two things
that *were* ours are both fixed (see Resolved): the reconnect is visible on the status chip, and
`serviceDiscoveryWatchdog` now bounds the discovery phase as well as the subscription pass.

Worth knowing before doing anything about the timeouts themselves: it is intermittent, it
recovered on its own the next time, and one observation is not a rate. **Measuring how often it
happens is the useful next step**, and now cheap — a stall that used to need a rider watching a
frozen screen is a logged `service discovery watchdog fired` line, and the 15:57 session on
2026-08-24 produced three of them in four minutes against a peripheral that had gone away.

---

## Recovery from a dead link is bounded, but not immediate

**Left open by the link watchdog, 2026-08-24.** Once the peripheral comes back, QZ takes roughly
13 s in the common case — 10 s to notice the silence, ~1 s of backoff, ~2 s to reconnect and
resubscribe — and up to ~18 s if the previous reconnect landed on a dead link and is working
through its own first-frame grace.

That floor is structural. QZ's only evidence that the link died is **silence**, so recovery
cannot be faster than the silence budget, and pressing Play on the bike is invisible to it.

The one thing that would make Play visible is a **scan**. A peripheral advertising as
connectable is proof it is not connected to you, and the fake bike starts advertising the
instant Play is pressed. Restarting discovery while the link is `lost` — and treating an
advertisement from the device we think we are connected to as immediate grounds to tear down —
would cut the latency to about a second.

It is a real change rather than a tuning knob: `bluetooth` owns the discovery agent,
`stopDiscovery()` is called when the device is claimed, and `bluetooth::finished` is not even
connected on Windows (`#ifndef Q_OS_WIN`), so a mid-session rescan is not a well-trodden path in
this tree.

*(An earlier version of this paragraph said a link that reaches `DiscoveredState` and never sends
a first frame is left alone. That was true of the watchdog's first draft and was fixed in the
same commit — `FIRST_FRAME_GRACE_MS`, 15 s from `ConnectedState` — before this entry was
written. The 17:17 log on 2026-08-24 shows it firing: `no Indoor Bike Data for 15155 ms`.)*

What a teardown cannot fix is the Windows un-bonded case in
[BUILDING-ON-WINDOWS.md](BUILDING-ON-WINDOWS.md) — services enumerate, no notifications are ever
delivered, and the remedy is an OS-level re-pair. QZ now loops on it visibly, with the backoff
climbing to the 5-minute ceiling, rather than sitting silent; that is honest but it is not a
cure, and nothing here has yet confirmed the ceiling firing on hardware.

### Done looks like

- Pressing Play on a bike QZ has lost gets it back in about a second, not thirteen.
- A rescan started mid-session does not disturb a link that is merely slow.

---

## The last-device address is remembered before the device has proved it is a trainer

**Left open 2026-08-24 by the wrong-device fix below.** `bluetooth::deviceDiscovered()` calls
`setLastBluetoothDevice(b)` at claim time — the moment the name matches, before a connection is
attempted, let alone before a frame arrives. So the address `connectToLastDeviceIfIdle()` falls
back on is whatever QZ last *tried*, not what last worked, and a ghost survives into the next
launch to be tried again.

It cost nothing in the 17:17 session, because the address it re-stored was the same stale one it
had come from. It is still the wrong invariant: the fallback exists to reach a device Windows is
already holding, and it should be seeded from a device that has actually delivered.

The question to answer first is what counts as proof. `connectedAndDiscovered` is the obvious
hook and is wrong — it fires on a descriptor write, which the 17:17 ghost would have reached had
it carried an FTMS service. The honest signal is the same one the watchdog uses: the first
Indoor Bike Data frame, which is where `noteLinkIsDelivering()` already runs.

Worth doing together with it: nothing ever clears the stored pair, so an address that is
permanently wrong can only be displaced by a successful connection to something else.

### Done looks like

- The stored address is one a frame has arrived from, not one a name matched.
- A ghost address does not survive a restart.

---

## The header hides under the display cutout, and the insets that would move it are dropped

**Found 2026-08-22 on the A34 (SM-A346M), from the device's own logs. Re-scoped 2026-08-24:**
the original write-up was against `main.qml` and the old `ToolBar`, both deleted with group F in
7c-2b. The C++/Java half is untouched and still broken, and the new UI has no inset handling of
its own, so the fault survives the tree it was found in.

`AndroidStatusBar.height` is 0 for the entire life of the process — see the comment at
[ToastArea.qml:9](../../src/ui/ToastArea.qml#L9), which is the reason that file does not use it.
Java computes the insets correctly and hands them over 54 ms before Qt exists:

```
17:28:40.083 CustomQtActivity: onApplyWindowInsets - Top:27 Bottom:48 Left:0 Right:0
17:28:40.083 CustomQtActivity: Raw insets   - SystemTop:75 SystemBottom:135 SystemLeft:0 SystemRight:0
17:28:40.083 CustomQtActivity: Cutout insets - Top:75 Bottom:0 Left:0 Right:0
17:28:40.137 QT: qt_process_init() called
```

`AndroidStatusBar::onInsetsChanged()` logs every change it accepts. That line appears **zero**
times in a capture taken with `log_debug` on — the same capture carrying dozens of QML warnings
from the same handler, so nothing was suppressing it. The values never reach C++.

### Three independent reasons it never arrives

[CustomQtActivity.java:119](../../src/android/src/CustomQtActivity.java#L119) already knows about
the race and gives up on it:

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
returning `new AndroidStatusBar()` ([androidstatusbar.cpp:28](../../src/androidstatusbar.cpp#L28))
and the constructor is what assigns `m_instance`. **No QML in this tree imports
`AndroidStatusBar` any more** — the old `main.qml` was the only importer — so the singleton is
never constructed, `instance()` is null forever, and the JNI entry point at
[androidstatusbar.cpp:73](../../src/androidstatusbar.cpp#L73) returns having done nothing. This
was "until QML first dereferences it" before 7c-2b; it is now unconditional.

Third: Java divides by `density` and sends dp, while Qt for Android measures QML in physical
pixels. 27 is not 75, so even a delivered value would be in the wrong unit.

### What the new UI does instead

Nothing. [Main.qml:60](../../src/ui/Main.qml#L60) is a bare `Item` with
`implicitHeight: unit * 4.4` and the tab strip anchored to its bottom, so on a device with a
cutout the tab row is what gets sliced. It presents better than the old drawer button did — a
tab is a wider hit target than an icon — which is exactly why it is worth fixing before someone
concludes it is fine.

### Why it has not shown up in a test

The emulator has no cutout. On `qz_test` (API 34, generic profile) the header renders complete,
because a top inset of 0 costs nothing when the true inset is also 0. Every device with a notch
or a punch-hole is affected and no AVD in use here is, so the whole existing test path is blind
to it.

This is upstream behaviour, not a strip regression: the guard was added by `07059849c`
("UnsatisfiedLinkError crash") and nothing in group A–F touched the inset path.

### Done looks like

- The header clears the status bar on the A34, with the whole tab strip touchable.
- `AndroidStatusBar.height` is non-zero, in the right unit, by the time the first frame is
  drawn, and still correct after a rotation.
- A device with a cutout and one without both render correctly, since the fix must not add
  padding where there is no inset to clear.

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
at the end ([server.cpp:144](../../src/qmdnsengine/src/src/server.cpp#L144)). On this phone Qt
enumerates nothing, so the walk does nothing and the restore writes back an **invalid**
interface - which plausibly clears the `IP_MULTICAST_IF` that the join fallback had just set.
The datagram then falls to `socket.writeDatagram(...)`, which follows a routing table whose only
entry is the `swlan0` link route.

If that is right, QZ can hear the query and cannot be heard answering it, which is exactly
the symptom. Two lines would settle it: skip the restore when the snapshot is invalid, and
re-apply `IP_MULTICAST_IF` before the fallback send. Neither has been done — both lines are
still as described.

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
([characteristicwriteprocessor.cpp:58](../../src/characteristics/characteristicwriteprocessor.cpp#L58)):

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

## The write log does not say which characteristic was written

**Found 2026-08-18, building the recorder.** `processWriteQueue()` logs
`" >> " + bytes + " // " + info`
([ftmsbike.cpp:208](../../src/devices/ftmsbike/ftmsbike.cpp#L208)). The `WriteRequest` it is
logging carries a `characteristic`, and the line does not print it.

Every `<<` line records its UUID, so a recorded fixture knows exactly which characteristic each
notification arrived on — and then has to store `?` for every write. In practice they are all
the control point, but "in practice" is not something a fixture should encode, and the moment
a write goes somewhere else the recording is quietly wrong rather than visibly incomplete.

One line. `qzlog2ride.py` will pick it up with no change, and recordings made afterwards will be
complete; older ones keep their `?`.

---

## The build banner reports a stale commit on an incremental build

**Found 2026-08-24, checking which binary produced a log.** The `QZ build` line said
`69009d225`. The binary had been built from `4af0cb28d`, and HEAD was `efa6b2891` by then — so
the banner named a commit two ahead of nothing and one behind reality.

`QZ_GIT_SHA` is a `-D` on the compile line (`src/qdomyos-zwift.pri:96`). `qmake` re-evaluates it
on every run, but `nmake` decides what to rebuild from file timestamps, and nothing in
`main.cpp`'s dependency list changes when only a define does. So `main.o` keeps whatever SHA it
was compiled with until something else forces it to rebuild, and the banner reports the commit
of the last `main.cpp` *compile* rather than of the build.

It is correct after `-Clean` and silently wrong otherwise, which is the bad combination: it
looks authoritative and there is no signal that it is stale.

The comment above that block says the stamp exists because *"answering 'am I running the new
binary?' by grepping ASCII out of the .exe cost more than one debugging round"*. That is exactly
what it cost again — and grepping ASCII does not work either, since `QStringLiteral` stores
UTF-16, so the search has to be `s.encode('utf-16-le')`.

[BUILDING-ON-WINDOWS.md](BUILDING-ON-WINDOWS.md) tells a reader to use this banner to confirm
that an `-DeployTo` of the `.exe` alone is still valid against the deployed Qt DLLs. That advice
is currently unsound.

Cheapest honest fix is to make the SHA a generated header that `main.cpp` includes, so the
dependency is a file and the rebuild follows from it. Writing it only when the contents change
keeps incremental builds cheap.

### Done looks like

- The banner names the commit the binary was built from, on an incremental build.
- Or, failing that, it says `unknown` rather than something plausible and wrong.

---

## `RideScenario`'s `bike` directive is still read by nothing

**Left over from the device-profile seam, 2026-08-21.** `applyDeviceProfile()` exists and
`simulatedFtmsBike` calls it from its constructor, so a test can now be the bike it says it is —
but it is hard-coded to the YPBM profile. The scenario format has carried a `bike` directive
since phase 0 ([ridescenario.h:25](../../src/devices/simulatedbike/ridescenario.h#L25)) and
nothing parses it into that call.

Small job now that the seam is there, and the thing that would let one scenario file exercise a
different device profile without a new harness.

---

# Resolved

Collapsed records. The detail is wherever the work was written up.

**~~QZ does not tell the rider when the bike goes away~~** — *resolved 2026-08-24.* The UI
refactor replaced `trainerConnected`/`appConnected` (booleans that latched on the first frame and
never came back down) with `trainerState`/`appState`, fed by `LinkStatus` and a frame-age test;
each chip carries its own age, the reconnect countdown is visible, and the ladder stops at a
5-minute ceiling. The product question — clear, grey out, or hold with a marker — was answered
**hold with a marker**: the last reading stays, dimmed, with its age in amber. See
[UI-INSTRUMENT-CLUSTER.md](UI-INSTRUMENT-CLUSTER.md) section 3.

**~~Windows never reports the disconnect at all~~** — *found and fixed 2026-08-24.* The entry
above had claimed the transport half was fine, quoting a log with `InvalidService` on every
service; no desktop log in this tree contains `InvalidService` at all, so that observation was
Android and read as general. On Qt 6 / WinRT the peripheral calling `cancelConnection()` produces
**nothing** — the controller sat in `DiscoveredState` for 2m06s after the last frame. Fixed by
`ftmsbike::update()` hanging up on a Discovered link that has not delivered for 10 s (15 s grace
before the first frame ever arrives), with unacknowledged writes shortening the wait to 2 s as
corroboration only; plus `retryNow()` hanging up first, since `connectToDevice()` returns early
unless the controller is `Unconnected`, and `bluetooth::rescan()` becoming reachable. The backoff
and the ceiling reset in `noteLinkIsDelivering()`, on a frame — not in the `connected` lambda,
where a bike that connects and never streams reset them every lap. Covered by
`tst/Devices/TestFtmsLinkWatchdog.h`. What it does not make instant is its own entry above.

**~~A connection that succeeds against the wrong device~~** — *found and fixed 2026-08-24.* With
no trainer to find, discovery came up empty and `connectToLastDeviceIfIdle()` handed the stored
address to `deviceDiscovered()`. Windows answered in **45 ms** — out of the bond record, not over
the air; a real connect in these logs takes ~2 s — and enumerated six cached services: exactly
the seven the working session had, minus `0x1826`. Nothing subscribed, no frame could arrive, and
QZ reported a live trainer until the stall watchdog pulled it down 15 s later, repeatedly. The
stored address was stale because the fake bike is a phone and Android randomises its BLE address;
the fallback is left alone, being correct for the real YPBM. Fixed where a *positive* answer
exists: `serviceScanDone()` refuses a device with no Fitness Machine service and hangs up, and
after `NO_FTMS_BEFORE_RESCAN` such connections in a row emits `deviceHasNoFtmsService()` —
connected **queued**, because the slot deletes the sender — to `bluetooth::rescan()`, since
reconnecting can only reach the same wrong device. Guarded on `everDeliveredThisSession`: a
trainer that streamed and then dropped is never torn down automatically. This also promotes the
stale GATT cache in [BUILDING-ON-WINDOWS.md](BUILDING-ON-WINDOWS.md) from "currently cosmetic" to
a fault with a symptom. Not unit-tested — the check needs a live `QLowEnergyController`, which
the Layer B harness by design does not have.

**~~The reconnect is invisible~~** — *fixed 2026-08-24* by the same refactor: the chip shows
searching, connecting, discovering, live, stale, lost and gave up, with the countdown to the next
attempt and `servicesFound` ticking up so a crawl is distinguishable from a stall.

**~~Nothing bounds the discovery phase~~** — *fixed 2026-08-24.* `serviceDiscoveryWatchdog` is
armed at `ConnectedState` and stopped at `DiscoveredState`, so it covers discovery as well as the
subscription pass it always covered; `serviceScanDone()` re-arms it so each phase gets its own
budget. Armed at `ConnectedState` rather than at `connectToDevice()` deliberately — the connect
attempt has its own ~23 s timeout in the WinRT stack below us, and racing it would turn a slow
radio into a retry loop. `serviceDiscoveryTimeout()` branches: with services present it forces
the subscription pass, without them it hangs up.

**~~The device-name branches in ftmsbike are unreachable from a test~~** — *resolved 2026-08-21.*
`applyDeviceProfile()` is the seam — the half of `deviceDiscovered()` that does not touch the
radio — and `simulatedFtmsBike` calls it from its constructor, so the default harness arrives as
a YPBM with `resistance_lvl_mode` set, ERG unsupported and 32 levels. It went in for
`TestErgSimConflict`, which needs that profile to reach the continuous-ERG block at all. The
`bike` directive it was meant to read is its own entry above.

**~~The QML destruction-order warnings at exit~~** — *fixed 2026-08-23.* Fourteen
`TypeError: Cannot read property 'trainerConnected' of null` at every quit; `RideState` is
declared before the engine now, so the engine is torn down first. Worth recording only because
those warnings are what surfaced the latching-status problem above.

**Not fixed, and nothing depends on them:** `bluetooth` still never clears the device on a clean
disconnect, and the fourteen commented-out `disconnected()` connections in
`src/devices/bluetooth.cpp` are still there. The link phase is read off the surviving bike object
instead.
