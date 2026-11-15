#!/usr/bin/env python3
# Draws res/DriftMatrix.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/DriftMatrix.cpp (the numbers in the LAYOUT block are the same in both).
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

H = 128.5
BG = "#ECE5D8"; INK = "#26262A"; QUIET = "#6B6660"; ACC = "#CF5A2E"; LINE = "#CFC6B6"
PLATE = "#2B2C31"; DISP = "#1D1E22"; ORANGE = "#EF7A37"; TEAL = "#1F8FA0"; CREAM = "#F3EFE8"

# ---- LAYOUT (same numbers in src/DriftMatrix.cpp) ----
W = 182.88  # 36 HP
DISP_X, DISP_Y, DISP_H = 8.0, 18.5, 10.5
Y1 = 42.0
X_MODE, X_CHANCE, X_CHANCECV, X_RADICAL, X_RADICALCV = 14.0, 37.0, 51.0, 66.0, 79.0
X_RESEED, X_TRIG, X_SLEW, X_SLEWCV, X_GAIN = 96.0, 108.0, 125.0, 138.0, 158.0
Y2 = 60.5
X_STORE, X_SLOT0, SLOT_DX = 13.0, 25.0, 8.4
X_A, X_B, X_MIX, X_MIXMODE, X_MIXCV, X_ABCV = 98.0, 111.0, 126.0, 140.0, 154.0, 168.0
JACK_ROW_Y = [80.5, 91.6, 102.7, 113.8]
X_IN0, X_OUT0, JACK_DX = 14.1, 100.04, 9.6

CONTROL = 3.2   # control label size (project standard)
JACK = 2.9      # jack label size (project standard)

fonts = {}


def text(s, x, y, size, font=FONT_BOLD, fill=INK, anchor="middle", spacing=0.04):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = {"middle": x - width * scale / 2, "end": x - width * scale}.get(anchor, x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')

# title bar
add(text("DRIFTMATRIX", 8.0, 13.2, 6.2, FONT_BOLD, INK, anchor="start", spacing=0.08))
add(text("32 x 32, unseen", W - 8.0, 13.2, 3.4, FONT_MED, QUIET, anchor="end"))
add(f'<rect x="8" y="15.35" width="{W - 16}" height="0.5" fill="{ACC}"/>')
# display window (text is drawn live by the module)
add(f'<rect x="{DISP_X}" y="{DISP_Y}" width="{W - 2 * DISP_X}" height="{DISP_H}" rx="1.2" fill="{DISP}"/>')


def switch_labels(x, y, title, up, down, side="right"):
    add(text(title, x, y - 6.0, CONTROL))
    if side == "right":
        add(text(up, x + 3.2, y - 1.4, 2.5, FONT_BOLD, QUIET, anchor="start"))
        add(text(down, x + 3.2, y + 3.6, 2.5, FONT_BOLD, QUIET, anchor="start"))
    else:
        add(text(up, x, y - 4.9 + 0.0, 2.5, FONT_BOLD, QUIET))


# ---- row 1
switch_labels(X_MODE, Y1, "MODE", "ON/OFF", "0-100%")
add(text("CHANCE", X_CHANCE, Y1 - 8.4, CONTROL))
add(text("CV", X_CHANCECV, Y1 + 1 - 6.0, JACK))
add(text("RADICAL", X_RADICAL, Y1 - 7.0, CONTROL))
add(text("CV", X_RADICALCV, Y1 + 1 - 6.0, JACK))
add(text("RESEED", X_RESEED, Y1 - 6.0, CONTROL))
add(text("TRIG", X_TRIG, Y1 + 1 - 6.0, JACK))
add(text("SLEW", X_SLEW, Y1 - 7.0, CONTROL))
add(text("CV", X_SLEWCV, Y1 + 1 - 6.0, JACK))
switch_labels(X_GAIN, Y1, "GAIN", "AUTO", "SUM")
# knob range marks
add(text("0", X_CHANCE - 6.6, Y1 + 7.9, 2.4, FONT_MED, QUIET))
add(text("100", X_CHANCE + 6.6, Y1 + 7.9, 2.4, FONT_MED, QUIET))
add(text("nudge", X_RADICAL - 4.6, Y1 + 7.6, 2.4, FONT_MED, QUIET))
add(text("all", X_RADICAL + 5.2, Y1 + 7.6, 2.4, FONT_MED, QUIET))
add(text("1ms", X_SLEW - 5.0, Y1 + 7.6, 2.4, FONT_MED, QUIET))
add(text("2s", X_SLEW + 5.2, Y1 + 7.6, 2.4, FONT_MED, QUIET))

add(f'<rect x="8" y="51.0" width="{W - 16}" height="0.35" fill="{LINE}"/>')

# ---- row 2: snapshots
add(text("STORE", X_STORE, Y2 - 6.0, CONTROL))
for s in range(8):
    add(text(str(s + 1), X_SLOT0 + s * SLOT_DX, Y2 - 5.0, CONTROL, FONT_BOLD, INK))
add(text("SNAPSHOTS", X_SLOT0 + 3.5 * SLOT_DX, Y2 + 6.8, 2.5, FONT_BOLD, QUIET))
add(text("A", X_A, Y2 - 6.6, CONTROL, FONT_BOLD, ACC))
add(text("B", X_B, Y2 - 6.6, CONTROL, FONT_BOLD, TEAL))
for x in (X_A, X_B):
    add(text("L", x - 4.6, Y2 + 7.0, 2.4, FONT_MED, QUIET))
    add(text("8", x + 4.8, Y2 + 7.0, 2.4, FONT_MED, QUIET))
add(text("MIX", X_MIX, Y2 - 6.6, CONTROL))
add(text("A", X_MIX - 5.0, Y2 + 7.0, 2.4, FONT_BOLD, ACC))
add(text("B", X_MIX + 5.0, Y2 + 7.0, 2.4, FONT_BOLD, TEAL))
add(text("SMOOTH", X_MIXMODE, Y2 - 5.0, 2.5, FONT_BOLD, QUIET))
add(text("COIN", X_MIXMODE, Y2 + 6.8, 2.5, FONT_BOLD, QUIET))
add(text("MIX CV", X_MIXCV, Y2 + 1 - 6.0, JACK))
add(text("A/B CV", X_ABCV, Y2 + 1 - 6.0, JACK))

# ---- jacks: inputs on paper, outputs on a dark plate
plate_x = W / 2 + 1.0
add(f'<rect x="{plate_x}" y="69.0" width="{W - 6.5 - plate_x}" height="{121.6 - 69.0}" rx="1.6" fill="{PLATE}"/>')
add(text("IN", 9.0, 72.0, CONTROL, FONT_BOLD, ACC, anchor="start"))
add(text("OUT", plate_x + 2.5, 72.4, CONTROL, FONT_BOLD, ORANGE, anchor="start"))
for r, y in enumerate(JACK_ROW_Y):
    for c in range(8):
        n = str(r * 8 + c + 1)
        add(text(n, X_IN0 + c * JACK_DX, y - 4.8, JACK, FONT_BOLD, INK))
        add(text(n, X_OUT0 + c * JACK_DX, y - 4.8, JACK, FONT_BOLD, CREAM))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
open(os.path.join(HERE, "DriftMatrix.svg"), "w").write(svg)
print("ok")

# --- Haer maker logo (res/haer_logo.py) on the bottom rail
import sys as _sys
_sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(os.path.join(HERE, "DriftMatrix.svg"), x=W / 2, y=126.0, cap=3.0, bg=BG)
