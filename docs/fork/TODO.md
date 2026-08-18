# To do

Things worth doing that are not being done yet, with enough evidence attached that picking one
up does not mean re-deriving why it matters. The same standard as the rest of these notes: what
was measured, where the seam is, and what "done" would look like.

Anything here that turns into work of its own gets a document and leaves a one-line pointer
behind.

---

## The device-name branches in ftmsbike are unreachable from a test

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
