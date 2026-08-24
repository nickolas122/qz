# Fork engineering notes

Working notes written while making the changes listed in [FORK.md](../../FORK.md). They
are a record of *why* — what was measured, what was ruled out, what turned out to be
wrong — not user documentation. Upstream's docs are in the folder above.

They are kept because the expensive part of this work was the evidence, and a note that
says "this was tested and it is not the cause" is worth more later than the fix itself.

## Current work

| | |
| --- | --- |
| [TODO.md](TODO.md) | Things worth doing that are not being done yet, with the evidence attached so picking one up does not mean re-deriving why it matters. Resolved entries are collapsed at the bottom. |

## The strip, and the UI it left behind

| | |
| --- | --- |
| [STRIP-SPEC.md](STRIP-SPEC.md) | Reducing QZ to a trainer bridge for Rouvy, Zwift, Kinomap and MyWhoosh: what was deleted, in what order, and the platform constraints that decided the shape. **All phases landed 2026-08-24**; the per-phase records at the end are the account of what each one actually cost. |
| [UI-INSTRUMENT-CLUSTER.md](UI-INSTRUMENT-CLUSTER.md) | The visual direction the stripped bridge is built in, and the two things the ride screen could not previously say: whether the trainer is there right now, and how much battery it has left. |
| [VIRTUAL-BIKE.md](VIRTUAL-BIKE.md) | Testing without the trainer in the room: a simulated bike the app runs against, a harness that feeds the real `ftmsbike` byte-exact FTMS frames, and the one scenario format both play. All six phases implemented. |

## Windows Bluetooth: the move to WinRT

| | |
| --- | --- |
| [WINDOWS-WINRT-PHASE0.md](WINDOWS-WINRT-PHASE0.md) | The case for moving off the Qt 5 Bluetooth path, and where the original argument for it did not survive contact with the source. |
| [WINDOWS-WINRT-BACKEND.md](WINDOWS-WINRT-BACKEND.md) | The plan for the backend swap. |
| [WINDOWS-BLE-HARDENING.md](WINDOWS-BLE-HARDENING.md) | Reconnect behaviour, service subscription, and the bond-cache problem that stopped the trainer working unpaired. |

## Windows on Qt 6

| | |
| --- | --- |
| [WINDOWS-QT6-PLAN.md](WINDOWS-QT6-PLAN.md) | The route to Qt 6 and the questions that decided its shape. |
| [WINDOWS-QT6-PHASE1.md](WINDOWS-QT6-PHASE1.md) | Toolchain measurements — why mingw loses and MSVC 2022 wins. |
| [WINDOWS-QT6-PHASE4.md](WINDOWS-QT6-PHASE4.md) | The result, including a prediction from Phase 0 that the bike disproved. |
| [BUILDING-ON-WINDOWS.md](BUILDING-ON-WINDOWS.md) | How a Windows build of this fork is actually obtained, and the traps worth naming. |

## The trainer, and the training apps

| | |
| --- | --- |
| [MEASURED-BIKE.md](MEASURED-BIKE.md) | What was measured about the trainer itself — that it publishes the commanded target rather than anything it measures, how fast the magnets actually move, and the two calibrations that rest on those numbers. |
| [DIRCON-SERVER-REFACTOR.md](DIRCON-SERVER-REFACTOR.md) | Why the DIRCON endpoint had to outlive the bike, and the refactor that gave it process lifetime. |
| [AUTO-ERG-MODE.md](AUTO-ERG-MODE.md) | The automatic ERG detector, parked, with an honest account of why. |
