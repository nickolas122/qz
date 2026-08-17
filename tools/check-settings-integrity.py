#!/usr/bin/env python3
"""Check that the four places a setting has to be declared still agree.

A setting in QZ is declared in qzsettings.h, registered in qzsettings.cpp's allSettings[]
with a count that must match, described in settings-catalog.json with a count that must
match, and bound by name in settings.qml. Nothing enforces any of it at compile time -
QML resolves settings by name at runtime, so a key that has been removed shows up as a
blank or dead control on the bike rather than as a build failure.

That is tolerable while settings are only ever added. It is not tolerable while several
hundred are being deleted, which is what this script exists for.

Exit code 0 if consistent, 1 otherwise.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"


def fail(problems):
    for p in problems:
        print(f"  - {p}")


def declared_in_header():
    """Setting names declared as `static const QString <name>;` in qzsettings.h."""
    text = (SRC / "qzsettings.h").read_text(encoding="utf-8", errors="replace")
    return set(re.findall(r"^\s*static\s+const\s+QString\s+(\w+)\s*;", text, re.M))


def registered_in_source():
    """(declared count, actual entry count) from qzsettings.cpp."""
    text = (SRC / "qzsettings.cpp").read_text(encoding="utf-8", errors="replace")

    m = re.search(r"allSettingsCount\s*=\s*(\d+)", text)
    declared = int(m.group(1)) if m else None

    # Entries look like {QZSettings::name, QZSettings::default_name},
    entries = re.findall(r"\{\s*QZSettings::(\w+)\s*,", text)
    return declared, entries


def catalog():
    """(settingCount field, actual array length, keys) from settings-catalog.json.

    `key` is the setting name; `name` is the human-readable label shown in the UI.
    """
    data = json.loads((SRC / "settings-catalog.json").read_text(encoding="utf-8", errors="replace"))
    entries = data.get("settings", [])
    keys = {e.get("key") for e in entries if isinstance(e, dict) and e.get("key")}
    return data.get("settingCount"), len(entries), keys


# settings.qml properties that have no key in qzsettings.h today. Most are QML-local
# machinery rather than settings - the search box state, the catalog loader, the language
# list. A few are real settings QML persists on its own: the theme_* colours, osc_ip and
# osc_port, gears_current_value_f, and crrGain/cwGain, which differ from the C++ CRRGain
# and CWGain only by case and so are separate QSettings keys entirely.
#
# All of this predates the strip. The list is a baseline rather than a target: the check
# fails when it GROWS, which is what catches a deletion that orphaned a live binding.
# Shrinking it is welcome and the script says so.
KNOWN_QML_ONLY = {
    "appLanguageOptions", "crrGain", "cwGain", "entry", "filteredSettings",
    "gears_current_value_f", "initialized", "nordictrack_fs5i_treadmill", "osc_ip",
    "osc_port", "searchableSettings", "settingsCatalog", "settingsCatalogError",
    "settingsCatalogLoaded", "settingsCatalogLoading", "settingsSearchActive",
    "settingsSearchPending", "settingsSearchVisible", "theme_background_color",
    "theme_status_bar_background_color", "theme_tile_background_color",
    "theme_tile_icon_enabled", "theme_tile_secondline_textsize", "theme_tile_shadow_color",
    "theme_tile_shadow_enabled",
}


def qml_properties():
    """Property names declared in settings.qml - these are the QML-visible settings."""
    text = (SRC / "settings.qml").read_text(encoding="utf-8", errors="replace")
    return set(re.findall(r"^\s*property\s+(?:int|bool|real|double|string|var|color)\s+(\w+)\s*:", text, re.M))


def main():
    problems = []

    header = declared_in_header()
    if not header:
        print("FAIL: parsed no settings out of qzsettings.h - the check itself is broken.")
        return 1

    # 1. qzsettings.cpp: the declared count must match the registered entries.
    declared_count, entries = registered_in_source()
    if declared_count is None:
        problems.append("could not find allSettingsCount in qzsettings.cpp")
    elif declared_count != len(entries):
        problems.append(
            f"qzsettings.cpp: allSettingsCount is {declared_count} but allSettings[] has {len(entries)} entries"
        )

    # 2. Every registered entry must actually be declared in the header.
    unknown = sorted(set(entries) - header)
    if unknown:
        problems.append(
            f"qzsettings.cpp registers {len(unknown)} name(s) not declared in qzsettings.h: {', '.join(unknown[:8])}"
        )

    # 3. settings-catalog.json: the count field must match the array.
    catalog_count, catalog_len, catalog_keys = catalog()
    if catalog_count != catalog_len:
        problems.append(
            f"settings-catalog.json: settingCount is {catalog_count} but the settings array has {catalog_len} entries"
        )

    # 4. Catalogued settings must exist. This is the one that catches a deletion that
    #    removed the C++ side and left the catalog describing a setting that is gone.
    orphan_catalog = sorted(catalog_keys - header - KNOWN_QML_ONLY)
    if orphan_catalog:
        problems.append(
            f"settings-catalog.json describes {len(orphan_catalog)} setting(s) that no longer exist "
            f"in qzsettings.h: {', '.join(orphan_catalog[:8])}"
        )

    # 5. The important one: settings.qml binds by name at runtime, so a key deleted from
    #    C++ while QML still binds it is a dead control on the bike, not a build error.
    #    Measured against the baseline, so only newly-orphaned bindings fail.
    qml = qml_properties()
    orphan_qml = qml - header
    new_orphans = sorted(orphan_qml - KNOWN_QML_ONLY)
    if new_orphans:
        problems.append(
            f"settings.qml binds {len(new_orphans)} setting(s) that were deleted from qzsettings.h: "
            f"{', '.join(new_orphans)}"
        )
    healed = sorted(KNOWN_QML_ONLY - orphan_qml)
    if healed:
        print(
            f"\nnote: {len(healed)} baselined QML-only propert(ies) are gone - "
            f"remove from KNOWN_QML_ONLY: {', '.join(healed[:8])}"
        )

    print(f"qzsettings.h        {len(header)} settings declared")
    print(f"qzsettings.cpp      {len(entries)} registered, allSettingsCount = {declared_count}")
    print(f"settings-catalog    {catalog_len} entries, settingCount = {catalog_count}")
    print(f"settings.qml        {len(qml)} properties bound")

    if problems:
        print(f"\nFAIL: {len(problems)} problem(s)")
        fail(problems)
        return 1

    print("\nOK: settings declarations are consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
