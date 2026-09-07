#!/usr/bin/env python3
"""Rasterise the QZ-lite mark from icons/qz-lite/qz-lite.svg into every size the two
shipped platforms ask for.

The SVG is the master and the only place the geometry lives; every PNG and the .ico
under src/ are generated. If you change the mark, change the SVG and re-run this - the
rasters are build output that happens to be committed, because neither the qmake build
nor the Android packager will run a generator for us.

    python tools/make-icons.py            # write the files
    python tools/make-icons.py --check    # fail if any of them is stale

Needs Pillow and nothing else. The SVG is deliberately written with only absolute
filled paths so that the parsing below is all the SVG this needs to understand - no
font, no renderer, no cairo.
"""
import argparse
import io
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, "icons", "qz-lite", "qz-lite.svg")
SS = 4  # supersampling factor; the antialiasing is the downsample

# Android's adaptive icon is a 108dp canvas whose inner 72dp is the only part guaranteed
# to survive the launcher's mask, so the foreground layer draws the mark at 2/3 scale.
ADAPTIVE_SAFE = 72.0 / 108.0

# A status-bar icon is an alpha mask the system tints, not a picture: white glyph,
# transparent everywhere else, 24dp with the mark nearly filling it.
STATUS_FILL = 0.85 / 0.615  # the glyph is 0.615 of the master's canvas

# What gets written where. Sizes are px.
DRAWABLE = {"ldpi": 32, "mdpi": 48, "hdpi": 72}
MIPMAP = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
STATUS_BAR = {"mdpi": 24, "hdpi": 36, "xhdpi": 48, "xxhdpi": 72, "xxxhdpi": 96}
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


# --- the smallest SVG reader that can read this one file ---------------------------

_PATH_RE = re.compile(r'<path\b[^>]*\bfill="(#[0-9A-Fa-f]{6})"[^>]*\bd="([^"]+)"')
_TOKEN_RE = re.compile(r"[A-Za-z]|-?\d*\.?\d+(?:[eE][-+]?\d+)?")


def parse_svg(path):
    """@return (viewbox_size, [(rgb, [contour, ...]), ...]) with contours as point lists."""
    src = open(path, encoding="utf-8").read()
    m = re.search(r'viewBox="0 0 (\d+(?:\.\d+)?) (\d+(?:\.\d+)?)"', src)
    if not m or m.group(1) != m.group(2):
        raise SystemExit("expected a square viewBox starting at the origin")
    size = float(m.group(1))
    out = []
    for fill, d in _PATH_RE.findall(src):
        rgb = tuple(int(fill[i:i + 2], 16) for i in (1, 3, 5))
        out.append((rgb, flatten(d)))
    if not out:
        raise SystemExit(f"no filled paths in {path}")
    return size, out


def flatten(d, steps=48):
    """Turn one path's `d` into polygons. Absolute M/L/H/V/Q/C/Z only, which is what the
    master uses; anything else is a sign the SVG was hand-edited into something this
    cannot read, so it raises rather than drawing it wrong."""
    toks = _TOKEN_RE.findall(d)
    i = 0
    cmd = None
    cur = (0.0, 0.0)
    start = (0.0, 0.0)
    contours, pts = [], []

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    def close():
        nonlocal pts
        if len(pts) > 2:
            contours.append(pts)
        pts = []

    while i < len(toks):
        t = toks[i]
        if t.isalpha():
            cmd = t
            i += 1
            if cmd in "Zz":
                close()
                cur = start
                continue
        if cmd is None:
            raise SystemExit(f"path data starts with a coordinate: {d[:40]}")
        if cmd == "M":
            close()
            cur = start = (num(), num())
            pts = [cur]
            cmd = "L"  # SVG says extra coordinate pairs after a moveto are linetos
        elif cmd == "L":
            cur = (num(), num())
            pts.append(cur)
        elif cmd == "H":
            cur = (num(), cur[1])
            pts.append(cur)
        elif cmd == "V":
            cur = (cur[0], num())
            pts.append(cur)
        elif cmd == "Q":
            c = (num(), num())
            end = (num(), num())
            for s in range(1, steps + 1):
                u = s / steps
                pts.append((
                    (1 - u) ** 2 * cur[0] + 2 * (1 - u) * u * c[0] + u * u * end[0],
                    (1 - u) ** 2 * cur[1] + 2 * (1 - u) * u * c[1] + u * u * end[1]))
            cur = end
        elif cmd == "C":
            c1 = (num(), num())
            c2 = (num(), num())
            end = (num(), num())
            for s in range(1, steps + 1):
                u = s / steps
                v = 1 - u
                pts.append((
                    v ** 3 * cur[0] + 3 * v * v * u * c1[0] + 3 * v * u * u * c2[0] + u ** 3 * end[0],
                    v ** 3 * cur[1] + 3 * v * v * u * c1[1] + 3 * v * u * u * c2[1] + u ** 3 * end[1]))
            cur = end
        else:
            raise SystemExit(f"unsupported path command {cmd!r} - see the docstring")
    close()
    return contours


def signed_area(pts):
    a = 0.0
    for j in range(len(pts)):
        x0, y0 = pts[j]
        x1, y1 = pts[(j + 1) % len(pts)]
        a += x0 * y1 - x1 * y0
    return a / 2.0


def fill_path(img, contours, rgb, transform):
    """Paint one path. Contours wound like the largest one are solid, the rest are holes -
    which is nonzero winding for the shapes this file contains (a rounded rectangle, and
    a Q whose counter is reverse-wound). It is not a general nonzero rasteriser."""
    if not contours:
        return
    ref = 1.0 if signed_area(max(contours, key=lambda c: abs(signed_area(c)))) > 0 else -1.0
    mask = Image.new("L", img.size, 0)
    md = ImageDraw.Draw(mask)
    for pts in sorted(contours, key=lambda c: -abs(signed_area(c))):
        solid = (signed_area(pts) > 0) == (ref > 0)
        md.polygon([transform(p) for p in pts], fill=255 if solid else 0)
    img.paste(Image.new("RGBA", img.size, rgb + (255,)), (0, 0), mask)


def render(size, paths, vb, glyph_only=False, scale=1.0, color=None):
    """@return an RGBA image of `size` px, drawn at SS x and downsampled."""
    c = size * SS
    img = Image.new("RGBA", (c, c), (0, 0, 0, 0))
    k = c / vb * scale
    off = (c - vb * k) / 2.0

    def tf(p):
        return (p[0] * k + off, p[1] * k + off)

    for rgb, contours in (paths[-1:] if glyph_only else paths):
        fill_path(img, contours, color or rgb, tf)
    return img.resize((size, size), Image.LANCZOS)


def circular(img):
    mask = Image.new("L", (img.width * SS, img.height * SS), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, mask.width - 1, mask.height - 1], fill=255)
    mask = mask.resize(img.size, Image.LANCZOS)
    out = img.copy()
    out.putalpha(ImageChops.multiply(img.getchannel("A"), mask))
    return out


# --- output ------------------------------------------------------------------------

def png_bytes(img):
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def ico_bytes(paths, vb):
    buf = io.BytesIO()
    biggest = render(max(ICO_SIZES), paths, vb)
    biggest.save(buf, "ICO", sizes=[(s, s) for s in ICO_SIZES])
    return buf.getvalue()


def targets(paths, vb):
    """@return {repo-relative path: bytes}."""
    out = {}
    out["icons/qz-lite/qz-lite-512.png"] = png_bytes(render(512, paths, vb))
    out["src/icons/icon.png"] = png_bytes(render(256, paths, vb))
    out["src/icons/qz-lite.ico"] = ico_bytes(paths, vb)
    for dpi, px in DRAWABLE.items():
        out[f"src/android/res/drawable-{dpi}/icon.png"] = png_bytes(render(px, paths, vb))
    for dpi, px in MIPMAP.items():
        img = render(px, paths, vb)
        out[f"src/android/res/mipmap-{dpi}/ic_launcher.png"] = png_bytes(img)
        out[f"src/android/res/mipmap-{dpi}/ic_launcher_round.png"] = png_bytes(circular(img))
        out[f"src/android/res/mipmap-{dpi}/ic_launcher_foreground.png"] = png_bytes(
            render(px, paths, vb, glyph_only=True, scale=ADAPTIVE_SAFE))
    for dpi, px in STATUS_BAR.items():
        out[f"src/android/res/drawable-{dpi}/ic_stat_qzlite.png"] = png_bytes(
            render(px, paths, vb, glyph_only=True, scale=STATUS_FILL, color=(255, 255, 255)))
    return out


def same_pixels(dest, data):
    """Compare what the file draws, not the bytes it is stored as - two Pillow versions
    encode the same image differently and --check should not care."""
    if not os.path.exists(dest):
        return False
    try:
        with Image.open(dest) as a, Image.open(io.BytesIO(data)) as b:
            if a.format != b.format:
                return False
            if a.format == "ICO":
                # An .ico is a bundle; Pillow hands back only its largest image unless
                # each size is asked for by name.
                if sorted(a.ico.sizes()) != sorted(b.ico.sizes()):
                    return False
                return all(a.ico.getimage(s).convert("RGBA").tobytes()
                           == b.ico.getimage(s).convert("RGBA").tobytes()
                           for s in sorted(a.ico.sizes()))
            return (a.size == b.size
                    and a.convert("RGBA").tobytes() == b.convert("RGBA").tobytes())
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="write nothing; exit 1 if a generated file is missing or stale")
    args = ap.parse_args()

    vb, paths = parse_svg(MASTER)
    stale = []
    for rel, data in targets(paths, vb).items():
        dest = os.path.join(ROOT, rel.replace("/", os.sep))
        old = os.path.exists(dest)
        if same_pixels(dest, data):
            continue
        if args.check:
            stale.append(rel)
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)
        print(("updated " if old else "wrote   ") + rel)

    if stale:
        print("stale, re-run tools/make-icons.py:", file=sys.stderr)
        for rel in stale:
            print("  " + rel, file=sys.stderr)
        return 1
    print("icons up to date" if args.check else "done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
