# Phase 4 — Verified against the bike

> Phase 4 of `WINDOWS-QT6-PLAN.md`. Phase 1 chose C1 (MSVC 2022 + official Qt 6); Phases 2-3
> stripped and ported. This is the phase that says whether the route paid for itself.

**It did.** The migration's whole purpose — running the trainer with no Windows pairing — works, and
the one warning class Phase 0 predicted would survive did not.

Measurements below are from `C:\qz-gamepad\debug-Sat_Aug_15_11_40_38_2026.log`, a Qt 6.8.2 / MSVC
2022 debug build against the FTMS trainer `YPBM001264` / "ELITE AVANTI", unless stated otherwise.

---

## 1. The `WINDOWS-BLE-HARDENING.md` §1 checklist

| Criterion | Result |
| --- | --- |
| Exactly one `all services discovered!` | **1** ✅ |
| `2ad9` indication subscribed | ✅ |
| `2ad2`, `2ad3`, `2ada`, `2a19` notifications subscribed | ✅ |
| Control granted and start acknowledged | **1** ✅ |
| `2ad2` streaming | **915** `characteristicChanged` ✅ |
| Reconnect on relaunch without re-pairing | ✅ (`c3bd6e84`) |

The count of eleven further `all services discovered` lines without the exclamation mark is a
different message, logged from `ftmsbike::stateChanged()` on each state transition. The §1 criterion
is the one emitted from `subscribeToServices()`, and it appears exactly once.

## 2. Unpaired operation — the point of the migration

**Zero** `Acesso negado` in the log. On Win32 every write was refused once the bond lapsed; that is
the failure that opened `WINDOWS-QT6-PLAN.md` Part 0. The device is `Unpaired` throughout.

`c3bd6e84` records the three things that had to be true, each measured rather than assumed: Windows
does GATT on an unpaired device; discovery cannot find a device the OS is already connected to, so
`bluetooth.cpp` gives discovery fifteen seconds and then hands the stored address to the same
`deviceDiscovered()` path; and `ftmsbike::update()` dereferenced `m_control` before the controller
existed, which Qt 5 forgave and Qt 6 faults on. Ten other devices had the same unguarded `update()`.

## 3. `ATT_ATTRIBUTE_NOT_FOUND` — Phase 0's prediction was wrong

**They are gone. Zero occurrences.** Phase 0 predicted they would persist, reasoning that every Qt
enumeration call uses the `Cached` overload on both backends and Qt 6 does not change that.

The reasoning was sound but incomplete, and `WINDOWS-BLE-HARDENING.md` §436 already contained the
missing half: those warnings were *"Windows serving handles from the bond record"* — `2a05`, `fff1`,
`fff2`, `d18d2c10-…`. The `Cached` overload reads a database the bond persists. With no bond there is
no persisted database to go stale, so the cached read returns what the current session discovered.
Removing the pairing removed the cache staleness as a side effect.

*(The mechanism is inference from the two facts; the absence of the warnings is measured.)*

This also retires the hardening doc's concern that "if `2ad2`/`2ad9` ever land in that set the only
remedy is unpair and re-pair" — on this route there is nothing to unpair.

Only two `qlowenergycontroller_winrt.cpp` warnings remain in a full session, both during
`connectToDevice()`, and neither prevents anything downstream.

## 4. Known noise, not defects

- **`write of opcode 0 could not be queued`**, twenty-odd times at startup. The FTMS handshake timer
  starts before `subscribeToServices()` has reached `2ad9` and captured `gattFTMSService`. It is a
  race, not a refusal: the attempt right after the capture succeeds and the session ends with control
  granted. The existing retry backoff already covers it. Reads like a failure; is not one.
- **`rootItem is not defined`** at startup, from `main.cpp` loading the engine before `homeform`
  installs the context property. Predates this work.
- **`Cannot assign to non-existent property "onTrainingProgramIntervalSoundRequested"`** in
  `Home.qml:50` — a dead binding, so interval sounds do not fire. Predates this work.

## 5. Everything else that had to hold

- **The test suite runs under MSVC.** 159 tests, 149 passed, 10 skipped, 0 failed — matching the
  mingw Qt 5 count of 149. It had been aborting: Qt 6 exports `QByteArray::toStdString()` from
  Qt6Core built at `_ITERATOR_DEBUG_LEVEL=2` (40-byte `std::string`) while our objects forced
  `_ITERATOR_DEBUG_LEVEL=0` (32 bytes), so the DLL wrote its layout into our slot. `detect_mismatch`
  cannot catch it, because an import library carries no such record. Qt 5 is immune — it defines
  `toStdString()` inline — so the fix is gated on `QT_MAJOR_VERSION` and `msvc2019` is untouched.
- **Android is not collateral damage.** The port had rewritten the QML imports in place, which breaks
  every Qt 5 target at *load* time while CI stays green. Qt 5 is now the source of truth and the Qt 6
  variant is generated at build time by `tools/qt6-qml-imports.py`. See that commit for the
  measurements.

## 6. Answers to the plan's open questions

- ~~Does the fork need a patched qtconnectivity on Qt 6 at all?~~ **No.** The only candidate was the
  stale-cache force-read, and §3 above removes the staleness it was for. This build runs stock
  Qt 6.8.2.
- ~~Do the `ATT_ATTRIBUTE_NOT_FOUND` warnings persist?~~ **No.** §3.
- **Is Android in scope after the strip?** Still open as a *product* question, but it is no longer
  urgent: the Qt 5 Android build is not broken by the Qt 6 work, so the decision can be taken on its
  merits rather than forced by a regression.

## 7. Not yet done

- The `window-qt6-build` CI job has never completed a run. Everything above is local.
- The device strip (Phase 2) removed the zoo, but `WINDOWS-QT6-PLAN.md` Part IV's end state — one
  bike — is not reached; the guards added in `c3bd6e84` touch ten devices that still exist.
