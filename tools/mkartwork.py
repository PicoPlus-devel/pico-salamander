#!/usr/bin/env python3
"""
mkartwork.py - build the pico-bootLoader menu tiles of pico-salamander.

    tools/capture_footage.sh ~/roms/arcade ~/tmp/salamander_footage
    tools/mkartwork.py --footage ~/tmp/salamander_footage --bootloader ../pico-bootLoader/emu/assets

writes DIR/themes/0/salamander.png (640x480) and DIR/themes/1/salamander.png
(320x240). Run the bootloader's tools/png2raw.py on the two PNGs afterwards to
build their .444/.555 caches. The screensaver image built into the firmware,
DefaultSS*.c, is made by tools/mkdefaultss.py.

The ROM sets are not part of this project, so neither are pictures of the
games: they are taken from your own ROM sets by the host harness
(tools/capture_footage.sh), then laid out here. The footage folder holds five
320x240 PPM frames (the game area is x 32-287):

    title.ppm      theme 0: the Salamander logo, top of the badge
    lf-title.ppm   theme 0: the Lifeforce logo, bottom of the badge
    left.ppm       theme 1: left panel
    middle.ppm     theme 1: middle panel
    right.ppm      theme 1: right panel

The tiles follow the two themes of pico-bootLoader, with the geometry of its
tools/make_category_art.py:

    theme 0   640x480: flat saturated ground, a huge ghosted silhouette (the
              Vic Viper, the player's ship), a boxed badge on the left, the
              wordmark and a rule
    theme 1   320x240: three slanted panels of game footage, white wordmark
              across the middle

Requires numpy and ffmpeg (with librsvg). Derived from pico-1943's
tools/mkartwork.py.
"""

import argparse
import base64
import os
import subprocess
import sys
import tempfile

import numpy as np


def load_footage(footage, name):
    path = os.path.join(footage, name + ".ppm")
    if not os.path.exists(path):
        sys.exit("%s not found - see tools/capture_footage.sh" % path)
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    if (w, h) != (320, 240):
        sys.exit("%s: expected a 320x240 picture" % path)
    return np.frombuffer(parts[3][:w * h * 3], np.uint8).reshape(h, w, 3)


def write_png(path, img):
    h, w, _ = img.shape
    with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(img.tobytes())
        ppm = f.name
    try:
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", ppm, path], check=True)
    finally:
        os.unlink(ppm)


def png_data_uri(img):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "a.png")
        write_png(path, img)
        with open(path, "rb") as f:
            return "data:image/png;base64," + base64.b64encode(f.read()).decode("ascii")


def rasterise(svg, out_png):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "tile.svg")
        with open(path, "w") as f:
            f.write(svg)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", path, "-frames:v", "1", "-update", "1",
                        "-pix_fmt", "rgb24", "-compression_level", "9", out_png], check=True)


def decode_png(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         check=True, capture_output=True).stdout
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                            "stream=width,height", "-of", "csv=p=0", path],
                           check=True, capture_output=True, text=True).stdout.strip().split(",")
    w, h = int(probe[0]), int(probe[1])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3)


# ---------------------------------------------------------------------------
# Theme 0, geometry from pico-bootLoader's tools/make_category_art.py
# ---------------------------------------------------------------------------
T0_W, T0_H = 640, 480
T0_GROUND, T0_TINT = "#9c1f3d", "#b23553"
BADGE_X, BADGE_Y, BADGE_W, BADGE_H = 2, 186, 90, 108
BADGE_FRAME = 8
RULE_Y, RULE_TH = 229, 2
DASH_X0, DASH_X1 = 94, 108
TEXT_X, TEXT_BASELINE, TEXT_SIZE = 114, 246, 45
WORDMARK = "SALAMANDER"

# The ghosted silhouette: the Vic Viper seen from above, nose to the right, in
# a 100x100 box. The upper half is drawn and mirrored. `knock` is punched back
# out of it in the ground colour.
SHIP_HALF = (
    '<path d="M97 50 L78 46 L50 43.5 L16 43 L8 45 L8 50 Z"/>'    # fuselage
    '<path d="M55 44 L40 29 L26 29 L18 44 Z"/>'                  # wing
    '<path d="M18 24.5 L72 25.5 L85 28 L72 30.5 L18 32 Z"/>'     # prong
    '<rect x="3" y="46" width="7" height="4"/>'                  # engine
)
SHIP_HULL = SHIP_HALF + '<g transform="matrix(1 0 0 -1 0 100)">' + SHIP_HALF + '</g>'
SHIP_KNOCK = (
    '<ellipse cx="70" cy="50" rx="8" ry="2"/>'                   # canopy
)
WATERMARK = (500, 350, 250, -12)  # centre x, centre y, box size, rotation

# The badge: the two logos, one above the other, on the black of the title
# screens. The rows of the 320x240 frames the logos are taken from, the
# columns that hold them, and the badge picture's size: the aspect of the
# badge's inside, 74x92.
LOGO_ROWS = {"title": (18, 137), "lf-title": (18, 114)}
LOGO_X0, LOGO_W = 34, 224
BADGE_ART_W, BADGE_ART_H = 224, 278
LOGO_GAP = 10
BADGE_GROUND = "#000000"


def badge_art(footage):
    crops = [load_footage(footage, name)[y0:y1, LOGO_X0:LOGO_X0 + LOGO_W]
             for name, (y0, y1) in LOGO_ROWS.items()]
    art = np.zeros((BADGE_ART_H, BADGE_ART_W, 3), np.uint8)
    y = (BADGE_ART_H - sum(c.shape[0] for c in crops) - LOGO_GAP) // 2
    for c in crops:
        art[y:y + c.shape[0]] = c
        y += c.shape[0] + LOGO_GAP
    return art


def theme0_svg(rule_x0, badge=None):
    cx, cy, size, rot = WATERMARK
    s = size / 100.0
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{T0_W}" height="{T0_H}">',
        f'<rect width="{T0_W}" height="{T0_H}" fill="{T0_GROUND}"/>',
        f'<g transform="rotate({rot} {cx} {cy}) translate({cx - size / 2},{cy - size / 2}) scale({s:.5f})">'
        f'<g fill="{T0_TINT}">{SHIP_HULL}</g><g fill="{T0_GROUND}">{SHIP_KNOCK}</g></g>',
    ]
    if rule_x0 is None:
        # measuring pass: the wordmark alone
        parts.append(
            f'<rect width="{T0_W}" height="{T0_H}" fill="#000"/>'
            f'<text x="{TEXT_X}" y="{TEXT_BASELINE}" font-family="Liberation Sans" font-weight="bold" '
            f'font-size="{TEXT_SIZE}" letter-spacing="2" fill="#ffffff">{WORDMARK}</text></svg>')
        return "".join(parts)

    ix, iy = BADGE_X + BADGE_FRAME, BADGE_Y + BADGE_FRAME
    iw, ih = BADGE_W - 2 * BADGE_FRAME, BADGE_H - 2 * BADGE_FRAME
    parts.append(
        f'<rect x="{BADGE_X}" y="{BADGE_Y}" width="{BADGE_W}" height="{BADGE_H}" fill="#ffffff"/>'
        f'<rect x="{ix}" y="{iy}" width="{iw}" height="{ih}" fill="{BADGE_GROUND}"/>'
        f'<svg x="{ix}" y="{iy}" width="{iw}" height="{ih}" viewBox="0 0 {BADGE_ART_W} {BADGE_ART_H}" '
        f'preserveAspectRatio="xMidYMid slice"><image xlink:href="{png_data_uri(badge)}" '
        f'width="{BADGE_ART_W}" height="{BADGE_ART_H}"/></svg>')
    parts.append(
        f'<rect x="{DASH_X0}" y="{RULE_Y}" width="{DASH_X1 - DASH_X0}" height="{RULE_TH}" fill="#ffffff"/>'
        f'<text x="{TEXT_X}" y="{TEXT_BASELINE}" font-family="Liberation Sans" font-weight="bold" '
        f'font-size="{TEXT_SIZE}" letter-spacing="2" fill="#ffffff">{WORDMARK}</text>'
        f'<rect x="{rule_x0}" y="{RULE_Y}" width="{637 - rule_x0}" height="{RULE_TH}" fill="#ffffff"/>')
    parts.append("</svg>")
    return "".join(parts)


def theme0_tile(out_png, footage):
    # measure the wordmark's ink, then start the rule 12px after it
    with tempfile.TemporaryDirectory() as d:
        probe = os.path.join(d, "m.png")
        rasterise(theme0_svg(None), probe)
        ink = (decode_png(probe) > 128).any(axis=2)
        cols = np.where(ink.any(axis=0))[0]
        rule_x0 = int(cols.max()) + 12
    rasterise(theme0_svg(rule_x0, badge_art(footage)), out_png)


# ---------------------------------------------------------------------------
# Theme 1: panels 8.5 degrees off vertical, 2px black seams
# ---------------------------------------------------------------------------
T1_W, T1_H = 320, 240
PANEL_CLIP = {
    "L": "0,0 120,0 84,240 0,240",
    "M": "120,0 236,0 200,240 84,240",
    "R": "236,0 320,0 320,240 200,240",
}
# x offset of each 320x240 picture in its panel, to centre the panel on what
# the picture shows: the ship and the formations, the growth and the
# tentacle, the tentacle from the ceiling
PANEL_X = {"L": -32, "M": 10, "R": 140}
T1_TEXT_Y, T1_TEXT_SIZE, T1_STROKE = 114, 33, "#101820"


def theme1_tile(out_png, footage):
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{T1_W}" height="{T1_H}"><defs>',
    ]
    for slot, pts in PANEL_CLIP.items():
        parts.append(f'<clipPath id="p{slot}"><polygon points="{pts}"/></clipPath>')
    parts.append(f'</defs><rect width="{T1_W}" height="{T1_H}" fill="#000"/>')
    for slot, name in zip("LMR", ("left", "middle", "right")):
        img = load_footage(footage, name)
        parts.append(f'<g clip-path="url(#p{slot})"><image xlink:href="{png_data_uri(img)}" x="{PANEL_X[slot]}" '
                     f'y="0" width="{T1_W}" height="{T1_H}"/></g>')
    parts.append('<g stroke="#000" stroke-width="2" fill="none"><path d="M120 0L84 240M236 0L200 240"/></g>')
    parts.append(
        f'<text x="{T1_W // 2}" y="{T1_TEXT_Y}" text-anchor="middle" font-family="Liberation Sans Narrow" '
        f'font-weight="bold" font-style="italic" font-size="{T1_TEXT_SIZE}" letter-spacing="1" '
        f'fill="#ffffff" stroke="{T1_STROKE}" stroke-width="3" paint-order="stroke fill">{WORDMARK}</text>')
    parts.append("</svg>")
    rasterise("".join(parts), out_png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--footage", metavar="DIR", required=True, help="frames from tools/capture_footage.sh")
    ap.add_argument("--bootloader", metavar="DIR", required=True, help="pico-bootLoader emu/assets directory")
    args = ap.parse_args()

    for theme, make in (("0", theme0_tile), ("1", theme1_tile)):
        path = os.path.join(args.bootloader, "themes", theme, "salamander.png")
        make(path, args.footage)
        print("wrote", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
