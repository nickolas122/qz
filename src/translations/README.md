# Translations

Two languages, because two are read here.

| Language | Catalogue | Locale code |
| --- | --- | --- |
| English | *(none)* | `en` |
| Portuguese (Brazil) | `qdomyos-zwift_pt_BR.ts` | `pt_BR` |

English has no catalogue and does not need one: it is the source language, so the string
inside every `qsTr()` in `../ui/*.qml` *is* the English UI. Picking English in Settings
uninstalls the translator rather than loading anything.

Upstream ships thirty languages. They were deleted here rather than left in place. Every
one of them was a catalogue of `homeform`'s strings — 1,539 of them in the Brazilian file
alone — and `homeform` is gone, along with the tiles, the charts, the workout editor and
the settings page they described. The `.qm` files still built and still shipped; not one
line of them appeared on screen. See [../../FORK.md](../../FORK.md).

## Changing it

The picker is in **Settings → Display → Language**, with three positions: *System*,
*Português* and *English*. It takes effect immediately — `QzLanguage` swaps the
translator and calls `QQmlEngine::retranslate()`, which re-evaluates every `qsTr()`
binding in the loaded tree — so there is no restart and no "changes apply on next
launch" note under it.

*System* means `QLocale::system()`. Any flavour of Portuguese resolves to this
catalogue; everything else falls through to the source strings.

The setting is stored under `app_language` as one of `auto`, `pt_BR` or `en`.

## Adding or changing a string

The catalogue is generated from the sources, so the source is the thing to edit:

```sh
lupdate -no-obsolete src/qdomyos-zwift.pri   # pull new qsTr()/tr() strings into the .ts
linguist src/translations/qdomyos-zwift_pt_BR.ts   # or edit the XML by hand
```

`-no-obsolete` matters: without it a string that has been deleted is kept as
`type="vanished"` rather than removed, which is how the file reached 1,539 entries for a
UI that no longer had them.

`.qm` files are compiled by the build (`CONFIG += lrelease` in `../qdomyos-zwift.pri`)
and embedded through `../translations.qrc`. They are build output — do not commit one.

A `<translation type="unfinished">` left in the file is a string that will appear in
English on a Portuguese screen. There is one language here and one person maintaining
it, so finish it in the same commit rather than leaving it for a workflow.
