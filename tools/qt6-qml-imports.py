#!/usr/bin/env python3
"""Rewrite the QML import lines for a Qt 6 build.

The .qml sources are written for Qt 5, which is what Android, iOS and every other
shipping target build with. Qt 5.15 refuses a library import that carries no
version - "Library import requires a version", and that includes QtQuick itself -
while Qt 6 does not offer the versions Qt 5 wants: QtMultimedia 5.15, QtCharts 2.2
and QtQuick.Dialogs 1.0 do not exist there, and QtGraphicalEffects does not exist
at all. QML has no preprocessor, so no single spelling satisfies both and one of
the two has to be produced at build time. Qt 5 is the one that ships, so it stays
the source of truth and Qt 6 is generated.

This copies the .qml files named by a .qrc into an output directory with their
imports rewritten, and emits a parallel .qrc whose aliases are unchanged - so
resource paths stay qrc:/Home.qml and nothing outside the build system can tell.
Non-QML entries are not copied; the generated .qrc points at them where they are.
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Modules whose Qt 5 version has no Qt 6 equivalent. The version is dropped rather
# than bumped: an unversioned import in Qt 6 means "the latest", which is what we
# want and what avoids pinning this table to a Qt 6 point release.
DROP_VERSION = [
    "QtQuick.Dialogs",
    "QtMultimedia",
    "QtCharts",
    "QtPositioning",
    "QtLocation",
    "QtWebView",
]

# QtGraphicalEffects was not carried into Qt 6; its types live in Qt5Compat.
RENAME = {"QtGraphicalEffects": "Qt5Compat.GraphicalEffects"}

# QtQuick, QtQuick.Controls, QtQuick.Layouts and QtQuick.Window are deliberately
# absent from both tables: Qt 6 still accepts their 2.x imports.

VERSION = r"[0-9]+(?:\.[0-9]+)?"


def rewrite(text):
    for module, replacement in RENAME.items():
        text = re.sub(
            rf"(?m)^(\s*import\s+){re.escape(module)}\s+{VERSION}\b",
            rf"\g<1>{replacement}",
            text,
        )
    for module in DROP_VERSION:
        text = re.sub(
            rf"(?m)^(\s*import\s+{re.escape(module)})\s+{VERSION}\b",
            r"\g<1>",
            text,
        )
    return text


def write_if_changed(path, data):
    """Leave the mtime alone when nothing moved, so nmake does not rebuild the world."""
    if path.exists() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qrc", required=True, help="source .qrc to mirror")
    ap.add_argument("--out-dir", required=True, help="where rewritten .qml files go")
    ap.add_argument("--out-qrc", required=True, help="generated .qrc to write")
    args = ap.parse_args()

    qrc = Path(args.qrc).resolve()
    src_dir = qrc.parent
    out_dir = Path(args.out_dir).resolve()
    out_qrc = Path(args.out_qrc).resolve()

    root = ET.parse(qrc).getroot()
    rcc = ET.Element("RCC")
    rewritten = 0

    for qresource in root.findall("qresource"):
        out_res = ET.SubElement(rcc, "qresource")
        if qresource.get("prefix") is not None:
            out_res.set("prefix", qresource.get("prefix"))

        for entry in qresource.findall("file"):
            rel = (entry.text or "").strip()
            # The alias is the resource path, and it has to survive verbatim: the
            # rewritten copy lives somewhere else on disk but must stay qrc:/Home.qml.
            alias = entry.get("alias") or rel
            source = src_dir / rel

            if rel.endswith(".qml") and source.is_file():
                # Read as bytes and hand back bytes, so CRLF or LF is whatever the
                # source file already had.
                original = source.read_bytes()
                text = original.decode("utf-8")
                new = rewrite(text).encode("utf-8")
                target = out_dir / rel
                if write_if_changed(target, new):
                    rewritten += 1
                where = target
            else:
                where = source

            out_file = ET.SubElement(out_res, "file")
            out_file.set("alias", alias)
            # rcc takes absolute paths, and forward slashes keep the XML readable
            # on Windows.
            out_file.text = where.as_posix()

    if hasattr(ET, "indent"):  # 3.9+; cosmetic, so not worth requiring
        ET.indent(rcc, space="    ")
    blob = ET.tostring(rcc, encoding="utf-8", xml_declaration=False) + b"\n"
    changed_qrc = write_if_changed(out_qrc, blob)

    print(f"qt6-qml-imports: {rewritten} qml rewritten, "
          f"{'wrote' if changed_qrc else 'unchanged'} {out_qrc.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
