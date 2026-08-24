# What was measured about the trainer

The FTMS trainer this fork exists for — `YPBM001264` / "ELITE AVANTI", a rebranded YPOO —
characterised over 3–9 August 2026 from its own logs and FIT files. Two shipped features
were calibrated against these numbers and cannot be re-derived from the code: the
resistance slew limiter and the ERG table's level selector.

Distilled from `PLANO-MEGAGYM-EXECUCAO.md`, which this replaces. That document was 1,300
lines of Portuguese working notes covering a physical-bridge project that was abandoned, a
Raspberry Pi topology the strip settled differently, and defects that are all fixed. What
is below is the part that is still load-bearing; the original is in git history.

---

## 1. The console publishes the target, not the wheel

**Power is a function of the commanded resistance level and cadence — nothing is
measured.** Proved two independent ways.

Over FTMS, in a structured workout: a command to level 16 at t=329.99 is answered at
t=330.67 with `R=16 W=136 cad=81`, 0.68 s later. A command to level 2 at t=420.00 is
answered 0.66 s later with `R=2 W=85 cad=81`.

By hand on the knob, with QZ sending nothing at all:

```
 t(s)     R      W    cad   W/rpm
 -1.00     4     68    66   1.030
 +0.00    22    140    68   2.059    <- +18 levels in ONE 1-second sample
 +1.01    22    152    72   2.111
```

Eighteen levels in a second with cadence flat and watts doubling. The magnets move at 2–3
levels/s (§3), so the physical move takes 6–9 s. The same log has 22→1 and 1→17 inside one
second each.

**Corroborating, and the fact that makes the rest follow:** 53 (level, cadence) pairs with
n ≥ 5 samples, **all 53 with exactly zero dispersion**. A machine that measured anything
would scatter.

This is why the fork's headline behaviour change — reporting the effort the rider actually
made rather than the requested value — was needed at all.

## 2. The physical bridge was abandoned, and why

Every variant: MITM on the harness, replacing the console, a SmartSpin2k on the knob, a
read-only tap. Decided 04/08/2026, and the reasoning is worth keeping because the gains
looked real until they were checked one by one.

| Claimed gain | Outcome |
|---|---|
| 12 hidden physical levels | **Do not exist** (§4) |
| Range above R=32 | No evidence; rested on the analysis §4 refutes |
| True physical resistance state | **Has a software substitute** — the slew limiter, §5 |
| Clean paddles | Delivered by OpenBikeControl, with no hardware |
| Deterministic latency | Irrelevant: 2–3 levels/s against milliseconds of BLE |
| Freeing the Pi's BLE radio | The only survivor — never measured, and has a cheap alternative |

Against that: the harness carries generator AC (measured), the console is probably
unrepairable on a rebranded YPOO, and replicating a position controller with asymmetric
rates is real embedded work rather than "drive a motor".

## 3. The actuator

| Direction | Rate | 19 levels | 5 levels |
|---|---|---|---|
| Up | 2–3 levels/s | 6.3 – 9.5 s | 1.7 – 2.5 s |
| Down | 4–5 levels/s | 3.8 – 4.8 s | 1.0 – 1.3 s |

The movement is **continuous**, not per-level bursts: the console takes an absolute target
and executes one move. The ~2× asymmetry suggests it works against something that helps on
the way back.

## 4. There is no dead zone

A dense sweep with cadence held at 81 rpm, filling the gaps that produced the earlier
"plateau at R 1–13" claim:

| R | W | Δ | R | W | Δ |
|---|---|---|---|---|---|
| 2 | 85 | — | 10 | 109 | +3 |
| 3 | 87 | +2 | 11 | 113 | +4 |
| 4 | 90 | +3 | 12 | 117 | +4 |
| 5 | 92 | +2 | 15 | 131 | +4.7 |
| 6 | 96 | +4 | 16 | 136 | +5 |
| 7 | 99 | +3 | 17 | 142 | +6 |
| 8 | 103 | +4 | 18 | 148 | +6 |
| 9 | 106 | +3 | | | |

The step grows smoothly from +2 to +6 W per level — no plateau, no elbow. That is the
convex curve magnetic braking produces.

The earlier plateau was an artefact of a table missing R 2–5 and 9–12 that averaged W/rpm
across mixed cadences. **W is not proportional to cadence** — R=16 gives 136 W at 81 rpm
and 131 W at 79 rpm — so the table is genuinely two-dimensional. Two earlier conclusions
fall with it: the dead zone is neither physical nor firmware hiding range. It does not
exist.

## 5. The slew limiter, and what is still owed

MyWhoosh's demand arrives at a ~1005 ms median and travels 15 levels between p5 and p95.
The actuator does 2–3 levels/s upwards. So on rolling terrain the target runs ahead of the
magnets and is never reached — and because the published watt **is** the target (§1), the
FIT file records an effort the legs did not make. The asymmetry biases the error one way:
light on the climbs, honest on the descents, accumulating over a session.

`src/devices/ftmsbike/resistanceslewlimiter.h` fixes that by never asking for more than the
actuator delivers. Defaults are `resistance_slew_up = 2`, `resistance_slew_down = 4`, both
**0 = off**, and at 0 the traffic is byte-identical to before.

Three things the implementation forced, which are not obvious from reading it:

- **The fractional level carries between ticks.** `poll_device_time` is 200 ms and at 2
  levels/s one level takes 500 ms. Discarding the remainder each tick would ramp at 1.67
  levels/s — slower than the magnets, adding lag instead of removing it. The internal clock
  advances by exactly `levels / rate`, not to *now*.
- **Instantaneous rate between two commands is not the metric.** Levels are integers, so a
  single one always lands slightly early or late; two consecutive commands at a 200 ms poll
  read as 2.5 levels/s with the rate set to 2. What holds — and what the test asserts — is
  that **distance travelled since the ramp began never exceeds `rate × elapsed`**.
- **A stalled poll buys nothing.** Credit is capped at 2 s: the magnets did not move during
  the gap either.

*Confirmed qualitatively* on 07/08: ramps of 4→26 in 11.0 s (2.0 levels/s) and 26→1 in
6.0 s (4.2 levels/s), and reported power began tracking the effort at the pedal.

**Still owed:** the quantitative pair — two ~20 min rides on the same route, one at `0` and
one at `2/4`, comparing mean power. The prediction is that the limited session reads
**lower**, because it stops counting effort that did not happen; if the mean does not fall,
the bias premise is wrong. It has to be ridden with the §6 selector fixes already in, or it
measures both changes at once.

*Tests:* `tst/Devices/TestResistanceSlewLimiter.h`, 14 cases including the distance
invariant and the measured durations of §3.

## 6. The ERG deficit was the table, not the keep-alive

On 07/08, 907 s with a live target: mean error `actual − target` = **−13.2 W**, and 22% of
the time more than 10 W below. It got worse as cadence fell (−84 W below 60 rpm) and worse
at low targets (74 W target → −19.4 W, at a mean level of 1.1).

**Not the slew limiter.** In steady state — target unchanged ≥15 s *and* resistance
unchanged ≥5 s, which excludes every ramp — the low-cadence deficit is there on both sides
of the limiter's introduction, with the magnets stationary.

The faults were in `src/ergtable.h` and its callers, all fixed:

| Where | Change |
|---|---|
| `estimateWattage` / `wattageAtResistance` | Extrapolate from the slope of the two nearest samples at both cadence ends; never below zero. It used to return a power measured at a *higher* cadence, so the selector thought a level paid more than it does and picked one lower |
| `estimateWattage` | A level with no samples is **interpolated** between its neighbours, not copied wholesale from the nearest one. R=5 copying R=6 (learned only between 92 and 100 rpm) made `estimate(5) = estimate(6) = 113 W` at every cadence — a wall that closed the bracket at R=4 for every target between ~70 and ~113 W |
| `resistanceFromPowerRequest` | Force the curve non-decreasing by *pool adjacent violators*, then take the **nearest** level rather than the one below. PAVA and not running-maximum: the maximum would fix the valleys but smear a peak across every level above it |
| `ftmsbike::resistanceFromPowerRequest` | Cadence 0 **holds** resistance instead of accepting the table's `1`, which used to drop R to the floor on every stop with a 12 s ramp back |
| `ftmsbike::ergResistanceAccepted` | A one-level change only passes if asked for continuously for 3 s. At 112 W the selection alternated 13↔14 once a second for 90 s |
| `ergTable::setResistanceReportsCommand` | Samples count during a ramp, because power here is a function of the *commanded* level (§1). The old 1 s window discarded 368 collections in one session — exactly the levels only visited in passing, which are the holes the row above interpolates |

**Verification ride, 08/08**, 1807 s with a live target — double the sample:

| cadence | before | after |
|---|---|---|
| < 60 | −84.1 W | **−12.5 W** |
| 60–65 | −45.8 W | **+1.1 W** |
| 65–70 | −20.4 W | **−2.4 W** |
| 70–75 | −6.6 W | +0.9 W |
| ≥ 75 | −4.2 W | +3.4 W |

Mean error −13.2 → **+2.7 W**; time more than 10 W below target 22% → **3%**. Across
targets from 83 W to 218 W everything lands inside ±2.4 W.

*Tests:* `tst/Erg/TestErgTableSelection.h`.

### The remaining deficit is the spiral protection

All of it. Cutting around the 50 rpm where it acts:

| band | n | mean target | error | mean R |
|---|---|---|---|---|
| < 50 (protection active) | 49 | 83 | **−21.6 W** | 21.4 |
| 50–60 | 30 | 74 | **+2.3 W** | 13.6 |
| 60–70 | 59 | 77 | **+0.1 W** | 8.7 |
| ≥ 70 | 1669 | 115 | +3.6 W | 7.7 |

Below 50 rpm the selector asks for more resistance and
[ftmsbike.cpp:963](../../src/devices/ftmsbike/ftmsbike.cpp#L963) refuses the increase — 110
blocks, concentrated between 36 and 47 rpm. That is the protection doing its job.

**Open, and a new question rather than the one this section answered:** whether the 50 rpm
threshold still makes sense now that the selector hits the target. Measure before touching
it; a correct selector makes the protection fire more, not less.

Also never exercised in the field: the cadence-0 resistance hold, 0 occurrences in the
verification ride.
