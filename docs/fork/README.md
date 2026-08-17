# Fork engineering notes

Working notes written while making the changes listed in [FORK.md](../../FORK.md). They
are a record of *why* — what was measured, what was ruled out, what turned out to be
wrong — not user documentation. Upstream's docs are in the folder above.

They are kept because the expensive part of this work was the evidence, and a note that
says "this was tested and it is not the cause" is worth more later than the fix itself.

## Current work

| | |
| --- | --- |
| [STRIP-SPEC.md](STRIP-SPEC.md) | **Draft.** Reducing QZ to a trainer bridge for Rouvy, Zwift, Kinomap and MyWhoosh: what is deleted, in what order, and the platform constraints that decide the shape. |
| [VIRTUAL-BIKE.md](VIRTUAL-BIKE.md) | **Draft.** Testing without the trainer in the room: a simulated bike the app runs against, a harness that feeds the real `ftmsbike` byte-exact FTMS frames, and the one scenario format both play. |

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

## Devices and training apps

| | |
| --- | --- |
| [DIRCON-SERVER-REFACTOR.md](DIRCON-SERVER-REFACTOR.md) | Why the DIRCON endpoint had to outlive the bike, and the refactor that gave it process lifetime. |
| [AUTO-ERG-MODE.md](AUTO-ERG-MODE.md) | The automatic ERG detector, parked, with an honest account of why. |
| [PLANO-MEGAGYM-EXECUCAO.md](PLANO-MEGAGYM-EXECUCAO.md) | Notes in Portuguese, from making the bike report the effort the rider actually made. |
