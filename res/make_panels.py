#!/usr/bin/env python3
# Draws the panel res/<Module>.svg. VCV Rack can't draw SVG <text>, so
# every label is turned into outline paths with fontTools. Positions are in mm
# and must match src/Layout.hpp.
#   pip install fonttools
#   python3 res/make_panels.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 132.08, 128.5  # 26 HP

BG, BG_DARK, INK, SOFT = "#F2F0EB", "#E0DCD3", "#1E2126", "#6B737C"
AMBER, CYAN, OUTBOX, OUT_TEXT = "#C2730F", "#17879C", "#1E2126", "#F2A93B"

# --- keep in step with src/Layout.hpp
DISP_X, DISP_Y, DISP_W, DISP_H = 6.0, 12.0, 71.7, 46.0
ENC_X, ENC_Y, ENC_SIZE = 93.68, 26.0, 16.0
PAGE_PREV_X, PAGE_NEXT_X, PAGE_Y = 86.18, 101.18, 45.0
SIDE_X, RUN_Y, RESET_Y, ACT_Y = 120.18, 19.0, 32.0, 45.0
GRID_X, GRID_Y, GRID_W, GRID_H = 6.0, 62.5, 120.08, 25.5
CTRL_X0, CTRL_DX = 6.0, 30.02
CTRL_KNOB_DX, CTRL_BTN_DX, CTRL_JACK_DX = 6.84, 15.82, 24.38
CTRL_Y, CTRL_BTN_Y, CTRL_ATT_Y = 101.5, 97.3, 105.6
JACK_Y = 120.0
IN_X = [9.5, 19.8, 30.1, 40.4, 50.7, 61.0]
OUT_X = [73.0, 83.0, 93.0, 103.0, 113.0, 123.0]
JACK_LABEL_Y = 114.9
FOOT_Y = 126.6

# what differs between the modules (keep in step with each module's Info)
MODULES = [
    dict(slug="Algo", title="ALGO", blurb="16 generative melody algorithms",
         aux_in=[("RESEED", "trig"), None], aux_out=("ACC", "accent"), action="RESEED"),
    dict(slug="Stochastic", title="STOCHASTIC", blurb="weighted-dice rhythm and melody",
         aux_in=[("NEW R", "trig"), ("NEW M", "trig")], aux_out=("SLIDE", "gate"), action="NEW"),
    dict(slug="Note", title="NOTE", blurb="64-step sequencer with probabilities",
         aux_in=[("FILL", "gate"), None], aux_out=("SLIDE", "gate"), action="FILL"),
    dict(slug="Indexed", title="INDEXED", blurb="steps with their own durations",
         aux_in=[("MOD A", "+-5 V"), ("MOD B", "+-5 V")], aux_out=("SLIDE", "gate"), action=None),
    dict(slug="Discrete", title="DISCRETE", blurb="a sweep crossing 32 thresholds",
         aux_in=[("INPUT", "+-5 V"), ("SCAN", "0-10 V")], aux_out=("SWEEP", "+-5 V"), action=None),
    dict(slug="PhaseFlux", title="PHASEFLUX", blurb="16 stages of shaped pulse bursts",
         aux_in=[None, None], aux_out=("STAGE", "trig"), action=None),
    dict(slug="Fractal", title="FRACTAL", blurb="a phrase looper that grows",
         aux_in=[("REC G", "gate"), ("REC CV", "1 V/oct")], aux_out=("ORN", "trig"), action="REC"),
    # all seven engines: the module draws the engine name, aux and action labels
    dict(slug="Multi", title="MULTI", blurb=None, aux_in=[None, None], aux_out=None, action=None),
]

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """An SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    if anchor == "middle":
        x0 = x - width * scale / 2
    elif anchor == "end":
        x0 = x - width * scale
    else:
        x0 = x
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        tp = TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y))
        gs[n].draw(tp)
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


def panel(mod):
    parts = []
    add = parts.append

    def line(x1, y1, x2, y2, color=BG_DARK, w=0.4, extra=""):
        add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{color}" stroke-width="{w}" {extra}/>')

    add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')

    # title strip
    add(text("X|FORMER", 6.0, 9.0, 2.6, FONT_BOLD, SOFT, anchor="start", spacing=0.12))
    add(text(mod["title"], 25.0, 9.0, 4.2, FONT_BOLD, AMBER, anchor="start", spacing=0.14))
    if mod["blurb"]:
        add(text(mod["blurb"], 25.0 + 3.6 * len(mod["title"]) + 3.0, 8.8, 2.4, FONT_MED, SOFT, anchor="start"))
    add(text("Haer", W - 6.0, 8.8, 2.6, FONT_BOLD, INK, anchor="end", spacing=0.08))

    # display and grid windows
    for (x, y, w, h) in ((DISP_X, DISP_Y, DISP_W, DISP_H), (GRID_X, GRID_Y, GRID_W, GRID_H)):
        add(f'<rect x="{x - 0.8}" y="{y - 0.8}" width="{w + 1.6}" height="{h + 1.6}" rx="2" fill="#07080A"/>')
        add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="1.2" fill="#0B0C0E"/>')

    # data encoder and page buttons
    add(text("DATA", ENC_X, 15.6, 3.2, FONT_BOLD, AMBER, spacing=0.1))
    add(f'<circle cx="{ENC_X}" cy="{ENC_Y}" r="{ENC_SIZE / 2 + 0.6}" fill="none" stroke="{BG_DARK}" stroke-width="0.8"/>')
    add(text("drag: value  click: next row", ENC_X, 36.6, 2.1, FONT_MED, SOFT))
    add(text("<  PAGE  >", (PAGE_PREV_X + PAGE_NEXT_X) / 2, PAGE_Y - 4.4, 3.2, FONT_BOLD, INK, spacing=0.06))

    # run / reset / action
    add(text("RUN", SIDE_X, RUN_Y - 4.6, 3.2, FONT_BOLD, INK, spacing=0.06))
    add(text("RESET", SIDE_X, RESET_Y - 4.6, 3.2, FONT_BOLD, INK, spacing=0.04))
    if mod["action"]:
        add(text(mod["action"], SIDE_X, ACT_Y - 4.6, 3.2, FONT_BOLD, AMBER, spacing=0.04))

    # CTRL knobs
    line(4.0, 91.0, W - 4.0, 91.0, AMBER, 0.25, 'opacity="0.5"')
    for k in range(4):
        bx = CTRL_X0 + CTRL_DX * k
        add(text(f"CTRL {k + 1}", bx + CTRL_KNOB_DX, 95.0, 3.2, FONT_BOLD, CYAN, spacing=0.06))
        add(text("CV", bx + CTRL_JACK_DX, 95.6, 2.9, FONT_BOLD, CYAN))
        if k:
            line(bx - 1.0, 93.0, bx - 1.0, 108.0, BG_DARK, 0.4)

    # jacks
    line(4.0, 110.0, W - 4.0, 110.0, AMBER, 0.25, 'opacity="0.5"')
    for x, lab, note in zip(IN_X, ["CLOCK", "RESET", "RUN", "V/OCT"], ["", "", "trig", "transp."]):
        add(text(lab, x, JACK_LABEL_Y, 2.9, FONT_BOLD, INK, spacing=0.0))
        if note:
            add(text(note, x, FOOT_Y, 2.1, FONT_MED, SOFT))
    for j, a in enumerate(mod["aux_in"]):
        if a:
            add(text(a[0], IN_X[4 + j], JACK_LABEL_Y, 2.9, FONT_BOLD, AMBER, spacing=0.0))
            add(text(a[1], IN_X[4 + j], FOOT_Y, 2.1, FONT_MED, SOFT))
    add(f'<rect x="{OUT_X[0] - 5.0}" y="111.2" width="{OUT_X[5] - OUT_X[0] + 10.0}" height="16.4" rx="1.2" fill="{OUTBOX}"/>')
    outs = ["GATE", "PITCH", "VEL", "TRIG", "EOC", mod["aux_out"][0] if mod["aux_out"] else None]
    for x, lab in zip(OUT_X, outs):
        if lab:
            add(text(lab, x, JACK_LABEL_Y, 2.9, FONT_BOLD, OUT_TEXT, spacing=0.0))
    if mod["aux_out"]:
        add(text(mod["aux_out"][1], OUT_X[5], FOOT_Y, 2.1, FONT_MED, "#9AA3AD"))
    add(text("OUT", (OUT_X[1] + OUT_X[2]) / 2 + 5.5, FOOT_Y, 2.1, FONT_MED, "#9AA3AD", spacing=0.1))

    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
        f'viewBox="0 0 {W} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
    )
    with open(os.path.join(HERE, mod["slug"] + ".svg"), "w") as fh:
        fh.write(svg)
    print("wrote " + mod["slug"] + ".svg")


# Only Multi is built now; the seven single-engine panels stay listed above
# for reference (their labels are what Multi draws live).
for m in MODULES:
    if m["slug"] == "Multi":
        panel(m)
