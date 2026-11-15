#!/usr/bin/env python3
# Mock-up picture of the panel with knobs, switches, buttons, jacks and the
# display drawn in, so the layout can be checked without Rack.
#   python3 tools/preview.py out.png
import os, sys, math, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
svg = open(os.path.join(HERE, "..", "res", "DriftMatrix.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_panel.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL =")[0].split("\n", 1)[1], L)
g = []
def jack(x, y):
    g.append(f'<circle cx="{x}" cy="{y}" r="4.1" fill="#C9CCD1"/><circle cx="{x}" cy="{y}" r="3.1" fill="#7C8088"/><circle cx="{x}" cy="{y}" r="1.9" fill="#0B0B0C"/>')
def knob(x, y, a, r=4.75):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#26272B" stroke="#3A3C42" stroke-width="0.5"/>')
    ang = math.radians(-135 + 270 * a)
    g.append(f'<line x1="{x}" y1="{y}" x2="{x + (r-0.8)*math.sin(ang):.2f}" y2="{y - (r-0.8)*math.cos(ang):.2f}" stroke="#EEE" stroke-width="0.7" stroke-linecap="round"/>')
def bezel(x, y, col):
    g.append(f'<circle cx="{x}" cy="{y}" r="3.3" fill="#2B2D33"/><circle cx="{x}" cy="{y}" r="2.5" fill="{col}"/>')
def switch(x, y, up):
    g.append(f'<rect x="{x-1.7}" y="{y-3.2}" width="3.4" height="6.4" rx="0.5" fill="#202020"/>')
    hy = y - 1.5 if up else y + 1.5
    g.append(f'<rect x="{x-1.3}" y="{hy-1.4}" width="2.6" height="2.8" fill="#CCCCCC"/>')
y1, y2 = L["Y1"], L["Y2"]
switch(L["X_MODE"], y1, False)
knob(L["X_CHANCE"], y1, 0.35, r=6.35)
knob(L["X_RADICAL"], y1, 0.15); knob(L["X_SLEW"], y1, 0.25)
bezel(L["X_RESEED"], y1, "#EF7A37")
switch(L["X_GAIN"], y1, True)
for x in (L["X_CHANCECV"], L["X_RADICALCV"], L["X_TRIG"], L["X_SLEWCV"]):
    jack(x, y1 + 1)
bezel(L["X_STORE"], y2, "#4a4a4a")
cols = {3: "#EF6A14", 6: "#19C7E6", 1: "#555", 2: "#555", 4: "#555"}
for s in range(8):
    bezel(L["X_SLOT0"] + s * L["SLOT_DX"], y2, cols.get(s + 1, "#34363b"))
knob(L["X_A"], y2, 3 / 8); knob(L["X_B"], y2, 6 / 8); knob(L["X_MIX"], y2, 0.4)
switch(L["X_MIXMODE"], y2, True)
jack(L["X_MIXCV"], y2 + 1); jack(L["X_ABCV"], y2 + 1)
for y in L["JACK_ROW_Y"]:
    for c in range(8):
        jack(L["X_IN0"] + c * L["JACK_DX"], y); jack(L["X_OUT0"] + c * L["JACK_DX"], y)
dx, dy, dh, W = L["DISP_X"], L["DISP_Y"], L["DISP_H"], L["W"]
F = 'font-family="Share Tech Mono, monospace"'
g.append(f'<text x="{dx+2.6}" y="{dy+dh*0.44}" {F} font-size="3.1" fill="#F3EFE8">0-100%  chance 35%  358 of 1024 live</text>')
g.append(f'<text x="{dx+2.6}" y="{dy+dh*0.84}" {F} font-size="2.8" fill="#B9B3A9">12 ins &gt; 9 outs patched</text>')
g.append(f'<text x="{W-dx-2.6}" y="{dy+dh*0.44}" {F} font-size="3.1" fill="#EF7A37" text-anchor="end">A 3  &gt; B 6</text>')
g.append(f'<text x="{W-dx-2.6}" y="{dy+dh*0.84}" {F} font-size="2.8" fill="#3FD0E0" text-anchor="end">mix 40%  smooth</text>')
for x in (7.6, W - 15.2):
    for y in (2.5, 126.0): g.append(f'<circle cx="{x}" cy="{y}" r="1.6" fill="#8A8D93"/>')
svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
tmp = out + ".svg"; open(tmp, "w").write(svg)
pw, ph = 915, 643
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="{os.path.basename(tmp)}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 2}});
await p.goto('file://{tmp}.html'); await p.waitForTimeout(300);
await p.screenshot({{path: '{out}'}}); await b.close(); }})();"""
open(out + ".js", "w").write(js)
subprocess.run(["node", out + ".js"], check=True, cwd="/tmp")
os.remove(out + ".js"); os.remove(tmp); os.remove(tmp + ".html")
print("wrote", out)
