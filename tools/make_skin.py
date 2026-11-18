#!/usr/bin/env python3
"""make_skin.py - draws the VHS Vibes cassette artwork and writes the hand-made MPC skin.

Run it on the PC that has the fonts (Windows; needs Pillow):

    python "plugins/VHS Vibes/tools/make_skin.py"

Reads   build/params.json            (written by tools/build.py; run a build first)
Writes  skins/TUI.json, skins/Q-Links.json and every PNG they use
        build/preview/page_N.png     (a mock-up of each page, for looking at on the PC)

tools/build.py copies skins/ into the plugin folder as "Plugin Skins/" unchanged.

The look: a hot pink VHS cassette. The reel window and two stickers sit along the top, the
controls live on a dark "face label" below. Captions and titles are baked into the page
backgrounds because the MPC can only draw live text in Titillium Web, Roboto or Nexa; the
value read-outs under the knobs are live (Nexa). The baked text is rendered from fonts
installed with Windows / Office (MS Gothic, Times New Roman, Brush Script MT). No font files
are shipped, only the pictures.

Moving parts: the plugin has read-only "meter" parameters whose value is a frame of a film strip.
Reel turns the two reel hubs; Level is the sun on the page sticker (its level, in the colour of
Sat Type's step: LEVEL_FRAMES frames per colour). Since 1.7 the sticker also shows what the sound
is doing, as a worn video picture would (the "picture effects", on with the Visual Vibes switch):
Grid is the neon grid, scrolling as Reel turns and bent by the wobble; Snow, Head, Streak and Haze
are transparent strips laid over the picture (snow for the hiss, the head switch strip along the
bottom, dropout streaks and the tracking band, the Hi Cut haze), frame 0 of each showing nothing.
The plugin decides every frame (see notifierMain in src/VHSVibes.cpp); the frame counts are read
from that file's kXxx constants, so the two cannot disagree. The words on the sticker are a picture
of their own over the sun, so one sun strip serves every page.
"""

import json
import math
import os
import random
import re
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
SDK = os.path.dirname(os.path.dirname(PROJECT))
sys.path.insert(0, os.path.join(SDK, "tools"))
import gen_ui as G  # noqa: E402  (the SDK's generator: reused for its JSON building blocks)

SKINS = os.path.join(PROJECT, "skins")
PREVIEW = os.path.join(PROJECT, "build", "preview")
PARAMS = os.path.join(PROJECT, "build", "params.json")

# --------------------------------------------------------------------------------------------
# Geometry (MPC tab area is 1280 x 628)
# --------------------------------------------------------------------------------------------
W, H = 1280, 628
S = 2                              # supersampling for the backgrounds
CELL_W = 140                       # one control cell
X0 = 80                            # left edge of the first cell (8 cells = 1120 px)
ROW_Y = [232, 416]                 # top of the two control rows (each 184 px)
ROW_H = 184
SLOT_DY, SLOT_H = 28, 152          # the touchable slot inside a row: below the block header
PANEL = (60, 228, 1220, 604)       # the dark face label that carries the controls
LABEL = (60, 22, 400, 210)         # title sticker
WINDOW = (440, 22, 840, 210)       # reel window
STICKER = (880, 22, 1220, 210)     # page sticker (sunset)
KNOB = 92                          # knob film-strip frame size
KNOB_FRAMES = 127

# Animated parts (see the note at the top). The frame counts come from the plugin (plugin_frames).
REEL_PARAM, LEVEL_PARAM = "Reel", "Level"
GRID_PARAM, SNOW_PARAM, HEAD_PARAM, STREAK_PARAM, HAZE_PARAM = "Grid", "Snow", "Head", "Streak", "Haze"
METERS = (REEL_PARAM, LEVEL_PARAM, SNOW_PARAM, HEAD_PARAM, STREAK_PARAM, GRID_PARAM, HAZE_PARAM)
SOURCE = os.path.join(PROJECT, "src", "VHSVibes.cpp")


def plugin_frames():
    """The film strips' frame counts, from the kXxx constants in the plugin's source."""
    with open(SOURCE, "r", encoding="utf-8") as fh:
        k = {m.group(1): int(m.group(2)) for m in re.finditer(r"^constexpr int (k\w+)\s*=\s*(\d+);", fh.read(), re.M)}
    f = {"reel": k["kReelFrames"], "level": k["kLevelFrames"], "colours": k["kSunColours"],
         "snow_steps": k["kSnowSteps"], "snow_vars": k["kSnowVariants"], "head_steps": k["kHeadSteps"],
         "head_vars": k["kHeadVariants"], "streaks": k["kStreakVariants"], "bands": k["kBandSteps"],
         "wobble_steps": k["kWobbleSteps"], "haze_steps": k["kHazeSteps"]}
    f["sun"] = f["level"] * f["colours"]
    f["grid"] = (1 + f["wobble_steps"]) * f["reel"]
    f["snow"] = 1 + f["snow_steps"] * f["snow_vars"]
    f["head"] = 1 + f["head_steps"] * f["head_vars"]
    f["streak"] = 1 + f["streaks"] + f["bands"]
    f["haze"] = 1 + f["haze_steps"]
    return f


FR = plugin_frames()
REEL_FRAMES = FR["reel"]           # one tooth-to-tooth step of the hub (60 degrees), and one line spacing of the grid
LEVEL_FRAMES = FR["level"]
HUB = 70                           # hub film-strip frame size
REEL_CENTRES = [(WINDOW[0] + 100, (WINDOW[1] + WINDOW[3]) // 2), (WINDOW[0] + 300, (WINDOW[1] + WINDOW[3]) // 2)]
STICKER_W, STICKER_H = STICKER[2] - STICKER[0], STICKER[3] - STICKER[1]
HORIZON = int(STICKER_H * 0.66)    # y of the horizon inside the sticker
SUN_BOX = (96, 16, 326, HORIZON)   # part of the sticker the sun film strip covers
GRID_BOX = (0, HORIZON, STICKER_W, STICKER_H)   # part the grid film strip covers
# The picture effects stay inside the sticker's paper border (3 px, rounded): the picture itself
FX_BOX = (3, 3, STICKER_W - 3, STICKER_H - 3)
HEAD_H = 20                                                           # the head switch strip's box, along the bottom
HEAD_BOX = (3, STICKER_H - 3 - HEAD_H, STICKER_W - 3, STICKER_H - 3)
GRID_BEND = [0.0, 3.25, 5.35, 8.15]   # px the grid's lines swing at the bottom edge, per wobble step (0: straight)
FX_STRENGTH = [0.25, 0.55, 0.95]      # head strip strength per step (as the approved mock-ups)
SNOW_STRENGTH = [0.15, 0.40, 0.65, 0.95]   # snow strength per step (one per kSnowSteps)
HAZE = (120, 92, 150)                 # the haze's colour; its opacity per step: haze_alpha()

# --------------------------------------------------------------------------------------------
# Palette
# --------------------------------------------------------------------------------------------
PINK = (255, 46, 151)
PINK_HI = (255, 120, 196)
PINK_LO = (204, 22, 116)
CYAN = (0, 240, 255)
PURPLE = (185, 103, 255)
YELLOW = (255, 249, 91)
ORANGE = (255, 139, 61)
NIGHT = (13, 2, 33)
PANEL_TOP = (25, 9, 60)
PANEL_BOT = (47, 16, 98)
PAPER = (253, 247, 255)
INK = (64, 16, 120)
# VHS mode: the vaporwave palette's mint (#05FFA1, the sister of PURPLE #B967FF), with a deeper seafoam
# below it and a pale mint above. It leans green, about 27 degrees of hue away from CYAN, so it does not
# read as one more cyan read-out.
SEAFOAM = (0, 196, 140)
MINT = (5, 255, 161)
MINT_PALE = (176, 255, 216)
# Hi-Fi mode (1.7): the sticker's sunset, a deep orange rising through amber to the sun's yellow.
EMBER = (255, 98, 36)
AMBER = (255, 164, 48)
SUN_PALE = (255, 240, 150)
# The sun in each of Sat Type's colours (1.7, with Visual Vibes): top and bottom of its gradient, the
# halo, and the colours the top and bottom go towards when it is loud. Simple's is the sun as it was.
SUN_COLOURS = [
    (YELLOW, PINK, (255, 120, 200), (255, 255, 255), (255, 200, 230)),   # Simple (and Visual Vibes off)
    (MINT_PALE, MINT, MINT, (255, 255, 255), MINT_PALE),                  # VHS
    (YELLOW, EMBER, AMBER, (255, 255, 255), SUN_PALE),                    # Hi-Fi
]

ARGB_CYAN = "ff00f0ff"
ARGB_BG = "ff0d0221"
ARGB_OVERLAY = "ff190a3c"

FONTS = "C:/Windows/Fonts/"
F_GOTHIC = (FONTS + "msgothic.ttc", 0)     # MS Gothic: captions and Japanese
F_TIMES = (FONTS + "timesbi.ttf", 0)       # Times New Roman Bold Italic: headers
F_BRUSH = (FONTS + "BRUSHSCI.TTF", 0)      # Brush Script MT: the logo
_font_cache = {}


def font(spec, px, scale=S):
    key = (spec, px, scale)
    if key not in _font_cache:
        _font_cache[key] = ImageFont.truetype(spec[0], int(round(px * scale)), index=spec[1])
    return _font_cache[key]


# --------------------------------------------------------------------------------------------
# Pages. A block is one Q-Link group of four cells; "<" continues the control to its left
# (a double-width control); None is an empty cell.
# --------------------------------------------------------------------------------------------
PAGES = [
    {"name": "TAPE", "jp": "テープ", "file": "bg_tape.png", "blocks": [
        ("PREAMP", "プリアンプ", ["Drive", "Tilt", "Slew", "Comp"]),
        ("OUTPUT", "アウトプット", ["Dropouts", "Mono", "Mix", "Output"]),
        ("VIBE", "雰囲気", ["Saturator", "Split", "Lows", "Lo Sat"]),
        ("", "", ["Highs", "Hi Sat", "Hi Cut", "Sat Type"]),
    ]},
    {"name": "WOBBLE", "jp": "ゆらぎ", "file": "bg_wobble.png", "blocks": [
        ("TRACKING", "トラッキング", ["Wobble On", "Warp", "Warp Rate", "Shape"]),
        ("FLUTTER", "フラッター", ["Flutter", "Flut Rate", "Scrape", None]),
        ("RE-RECORD", "ダビング", ["Wow", "Wow Rate", "Flutter 2", "Flut2 Rate"]),
        ("FILTER", "フィルター", ["Lo Cut", "Hi Cut 2", None, None]),
    ]},
    {"name": "NOISE", "jp": "ノイズ", "file": "bg_noise.png", "blocks": [
        ("NOISE", "ノイズ", ["Noise On", "Hum", "Buzz", "Head Buzz"]),
        ("", "", ["Hiss", "Crackle", "Noise", "Noise Pos"]),
        ("TV SET", "テレビ", ["Whistle", None, None, None]),
        ("", "", [None, None, None, None]),
    ]},
    {"name": "INPUT", "jp": "インプット", "file": "bg_input.png", "blocks": [
        ("CHORUS", "コーラス", ["Chorus", "Cho Mix", "Cho Speed", "Cho Delay"]),
        ("", "", ["Cho Width", None, None, None]),
        ("MIC", "マイク", ["Mic", "Mic Type", "<", "Mic Gain"]),
        ("", "", ["Mic Model", None, None, None]),
    ]},
    {"name": "DECK", "jp": "デッキ", "file": "bg_deck.png", "blocks": [
        ("DECK", "デッキ", ["Deck On", "AGC", "Drift", "AGC Model"]),
        ("ALIGNMENT", "アライメント", ["Tracking", "Breathing", "Azimuth", None]),
        ("SCREEN", "スクリーン", ["Animation", "Visual Vibes", None, None]),
        ("QUALITY", "高音質", ["HQ", None, "B-Type NR", "<"]),
    ]},
]

# Caption printed above a control (default: the parameter name in capitals).
CAPTIONS = {
    "Saturator": "POWER", "Chorus": "POWER", "Mic": "POWER",
    "Cho Mix": "MIX", "Cho Speed": "SPEED", "Cho Delay": "DELAY", "Cho Width": "WIDTH",
    "Mic Type": "TYPE", "Mic Gain": "GAIN", "Mic Model": "MIC MODEL", "AGC Model": "AGC MODEL",
    "Flutter": "DEPTH", "Flut Rate": "RATE",
    "Wow Rate": "RATE", "Flutter 2": "FLUTTER", "Flut2 Rate": "RATE",
    "Hi Cut 2": "HI CUT", "Noise": "MASTER", "Noise Pos": "POSITION",
    "Sat Type": "TYPE",
    "Wobble On": "POWER", "Noise On": "POWER",
    "Deck On": "POWER", "Visual Vibes": "VISUAL VIBES",
}
BIPOLAR = {"Tilt", "Lows", "Mic Gain"}
# The 16 parameters on the knobs in Program (track) Q-Link mode, Q-Link 1..16.
PROGRAM_QLINKS = ["Warp", "Flutter", "Wow", "Flutter 2", "Drive", "Slew", "Comp", "Hi Cut",
                  "Hiss", "Crackle", "Hum", "Noise", "Hi Sat", "Dropouts", "Mix", "Output"]

# Modes: a step of MODE_PARAM that gives some knobs another job. While the switch sits on a mode's step,
# those knobs are drawn in the mode's colours (arc and pointer), their caption is shown in the mode's
# colour (with the mode's name for the job, if it has one) and the switch's own button for that step
# lights in the mode's colours. The control under a knob stays the same parameter (and the same Q-Link).
# Step 0 (and any step without an entry here) is the plain look. The number of steps comes from the
# parameter's choices, so a new mode is one more entry here (plus its button in SEGMENTS).
#   sweep   arc colours from the knob's start, through its middle, to its end (as CYAN -> PURPLE -> PINK)
#   pointer the pointer colour; caption: caption colour; button: (left, right) fill of the lit button
#   knobs   parameter -> caption shown in this mode (None keeps the usual caption, recoloured)
MODE_PARAM = "Sat Type"
MODES = [
    {"key": "vhs", "step": 1,
     "sweep": (SEAFOAM, MINT, MINT_PALE), "pointer": MINT, "caption": MINT,
     "button": (SEAFOAM, MINT), "button_text": NIGHT, "button_edge": MINT_PALE,
     "knobs": {"Split": "BIAS", "Lo Sat": "REC LEVEL", "Hi Sat": "TAPE SPEED", "Azimuth": None}},
    # Hi-Fi: the three it renames, and the seven the FM path takes over (their names stay)
    {"key": "hifi", "step": 2,
     "sweep": (EMBER, AMBER, YELLOW), "pointer": AMBER, "caption": AMBER,
     "button": (EMBER, YELLOW), "button_text": NIGHT, "button_edge": SUN_PALE,
     "knobs": {"Split": "CAL", "Lo Sat": "REC LEVEL", "Hi Sat": "CARRIER",
               "Hiss": None, "Crackle": None, "Head Buzz": None,
               "Breathing": None, "Tracking": None, "Drift": None, "Dropouts": None}},
]
# Previews only: knobs of a mode shown turned up, (name, knob position 0..1, the plugin's read-out there)
MODE_SHOWCASE = {"vhs": [("Azimuth", 0.25, "3.8'"), ("Azimuth", 0.5, "15'")]}
CAPTION_H = 25                     # height of the caption strip at the top of a slot
CAPTION_COLOUR = (210, 250, 255)
CAPTION_OVERRIDE = {}              # name -> (caption, colour), used while a mode's captions are being painted
# Controls that are on the page only while MODE_PARAM is on one step (name -> step): the page's background
# leaves their slot empty, the whole slot (caption, knob seat, value window) is that mode's overlay, and the
# control itself is valid only on that step, so in the other modes there is nothing to see or touch.
MODE_ONLY = {"Azimuth": 1}


def mode_steps(params):
    return len(params[MODE_PARAM].choices)


def mode_at(step):
    for m in MODES:
        if m["step"] == step:
            return m
    return None


def knob_runs(name, steps):
    """[(first step, last step, mode or None)]: the switch steps split into runs that draw this knob alike."""
    runs = []
    for s in range(steps):
        m = mode_at(s)
        m = m if m is not None and name in m["knobs"] else None
        if runs and runs[-1][2] is m:
            runs[-1] = (runs[-1][0], s, m)
        else:
            runs.append((s, s, m))
    return runs


def step_handle(first, last, steps, handle):
    """The handle that is valid only while the switch is on steps first .. last."""
    if first == last:
        return "IndexedEnabling/%d/%d/%s" % (first, steps, handle)
    return "RangedEnabling/%d/%d/%d/%s" % (first, last, steps, handle)


def swap_file(name, mode):
    return "cap_%s_%s.png" % (name.lower().replace(" ", ""), mode["key"])


def knob_file(bipolar, mode=None):
    return ("knob_bi" if bipolar else "knob_uni") + ("_" + mode["key"] if mode else "") + ".png"


# Segment switches: parameter -> (button size, [(dx, dy) inside the slot], [captions])
SEGMENTS = {
    "Noise Pos": ((64, 40), [(4, 50), (72, 50)], ["POST", "PRE"]),
    # three alike in one column (1.7), as wide as Mic Model's and at its x; 32 px high with 6 px between
    # them so all three fit between the caption and the slot's bottom edge (28 .. 136)
    "Sat Type": ((130, 32), [(5, 28), (5, 66), (5, 104)], ["SIMPLE", "VHS", "HI-FI"]),
    "Mic Model": ((130, 40), [(5, 28), (5, 74)], ["SIMPLE", "MODELED"]),   # stacked, as Mic Type beside it: MODELED does not fit 64 px
    "AGC Model": ((130, 40), [(5, 28), (5, 74)], ["SIMPLE", "MODELED"]),   # the same, beside AGC
    "Mic Type": ((130, 40), [(7, 28), (143, 28), (7, 74), (143, 74)], ["CAMCORDER", "DYNAMIC", "PHONE", "BOOMBOX"]),
    "B-Type NR": ((130, 40), [(7, 28), (143, 28), (7, 74), (143, 74)], ["OFF", "REC", "PLAY", "ON"]),
}
TOGGLE_SIZE = (88, 44)
TOGGLE_POS = (26, 50)
KNOB_POS = (24, 26)
VALUE_BOX = (10, 122, 120, 26)     # x, y, w, h inside the slot


# --------------------------------------------------------------------------------------------
# Drawing helpers (all coordinates in final pixels; `s` scales to the supersampled canvas)
# --------------------------------------------------------------------------------------------
def sc(v, s=S):
    if isinstance(v, (tuple, list)):
        return tuple(int(round(x * s)) for x in v)
    return int(round(v * s))


def lerp_col(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def gradient(size, c0, c1, horizontal=False):
    w, h = size
    n = w if horizontal else h
    strip = Image.new("RGB", (n, 1))
    strip.putdata([lerp_col(c0, c1, i / max(1, n - 1)) for i in range(n)])
    if horizontal:
        return strip.resize((w, h))
    return strip.rotate(90, expand=True).transpose(Image.Transpose.FLIP_TOP_BOTTOM).resize((w, h))


def rounded_mask(size, radius):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius, fill=255)
    return m


def paste_rounded(img, box, fill_img, radius):
    """Paste fill_img (already box-sized, supersampled) into img with rounded corners."""
    x0, y0, x1, y1 = sc(box)
    fill_img = fill_img.resize((x1 - x0, y1 - y0)) if fill_img.size != (x1 - x0, y1 - y0) else fill_img
    img.paste(fill_img, (x0, y0), rounded_mask(fill_img.size, sc(radius)))


def text_width(text, fnt, tracking=0.0):
    return sum(fnt.getlength(ch) for ch in text) + tracking * S * max(0, len(text) - 1)


def draw_text(draw, xy, text, fnt, fill, tracking=0.0, anchor="l"):
    """Tracked text. xy in final pixels; anchor l / m / r picks which end xy[0] is."""
    x, y = xy[0] * S, xy[1] * S
    w = text_width(text, fnt, tracking)
    if anchor == "m":
        x -= w / 2
    elif anchor == "r":
        x -= w
    for ch in text:
        draw.text((x, y), ch, font=fnt, fill=fill)
        x += fnt.getlength(ch) + tracking * S
    return w / S


def gradient_text(img, xy, text, fnt, c0, c1, shadow=None, shadow_off=(3, 3), anchor="l"):
    """Text filled with a vertical gradient, with an optional offset shadow colour."""
    pad = 14 * S                      # room for italic / script overhang
    w = int(fnt.getlength(text)) + 2 * pad
    asc, desc = fnt.getmetrics()
    h = asc + desc + 2 * pad
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).text((pad, pad), text, font=fnt, fill=255)
    xy = (xy[0] - pad / S, xy[1] - pad / S) if anchor != "m" else (xy[0], xy[1] - pad / S)
    x, y = int(xy[0] * S), int(xy[1] * S)
    if anchor == "m":
        x -= w // 2
    if shadow:
        img.paste(Image.new("RGB", (w, h), shadow), (x + sc(shadow_off[0]), y + sc(shadow_off[1])), mask)
    img.paste(gradient((w, h), c0, c1), (x, y), mask)
    return w / S


def overlay(img, layer):
    """Alpha-composite an RGBA layer onto an RGB image in place."""
    base = img.convert("RGBA")
    base.alpha_composite(layer)
    img.paste(base.convert("RGB"))


# --------------------------------------------------------------------------------------------
# The cassette
# --------------------------------------------------------------------------------------------
def draw_shell(img):
    d = ImageDraw.Draw(img)
    shell = gradient(sc((W, H)), (255, 92, 176), (238, 28, 136))
    # plastic grain
    grain = Image.effect_noise(shell.size, 22).convert("RGB")
    shell = Image.blend(shell, ImageChops.multiply(shell, grain), 0.10)
    img.paste(shell, (0, 0), rounded_mask(shell.size, sc(30)))
    d.rounded_rectangle(sc((3, 3, W - 4, H - 4)), sc(28), outline=PINK_HI, width=sc(2))
    d.rounded_rectangle(sc((9, 9, W - 10, H - 10)), sc(23), outline=PINK_LO, width=sc(1))
    # grip ridges down both sides
    for i in range(4):
        for x in (20 + i * 9, W - 21 - i * 9):
            d.rounded_rectangle(sc((x - 2, 60, x + 2, H - 60)), sc(2), fill=PINK_LO)
            d.line(sc((x + 3, 62, x + 3, H - 62)), fill=PINK_HI, width=sc(1))
    # corner screws
    for cx, cy in ((30, 30), (W - 30, 30), (30, H - 30), (W - 30, H - 30)):
        d.ellipse(sc((cx - 9, cy - 9, cx + 9, cy + 9)), fill=PINK_LO, outline=(160, 10, 90), width=sc(1))
        d.line(sc((cx - 5, cy - 5, cx + 5, cy + 5)), fill=(255, 150, 210), width=sc(2))
        d.line(sc((cx - 5, cy + 5, cx + 5, cy - 5)), fill=(255, 150, 210), width=sc(2))
    # embossed strip along the bottom edge
    f = font(F_GOTHIC, 13)
    # the version, pressed into the plastic with the rest (full-width characters, from plugin.json)
    with open(os.path.join(PROJECT, "plugin.json"), "r", encoding="utf-8") as fh:
        version = json.load(fh).get("version", "")
    wide = "".join(chr(ord(c) + 0xFEE0) if "!" <= c <= "~" else c for c in "v" + version)
    strip = "ＶＨＳ　ＶＩＢＥＳ　／　ＨＩ－ＦＩ　ＳＴＥＲＥＯ　／　ビデオカセット　／　" + wide
    draw_text(d, (W / 2, 609), strip, f, (255, 170, 220), 1.0, "m")
    draw_text(d, (W / 2, 608), strip, f, (150, 8, 86), 1.0, "m")
    for x in (290, 990):
        for k in range(3):
            xx = x + k * 14 if x < W / 2 else x - k * 14
            pts = [(xx, 610), (xx, 622), (xx + (-9 if x < W / 2 else 9), 616)]
            d.polygon([sc(p) for p in pts], fill=(176, 14, 100))


def shadowed_box(img, box, radius):
    """A soft dark-pink shadow under a sticker so it sits on the shell."""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).rounded_rectangle(sc((box[0] + 2, box[1] + 4, box[2] + 3, box[3] + 6)), sc(radius), fill=(120, 0, 70, 150))
    overlay(img, layer.filter(ImageFilter.GaussianBlur(sc(3))))


def draw_title_label(img):
    x0, y0, x1, y1 = LABEL
    shadowed_box(img, LABEL, 10)
    paper = gradient(sc((x1 - x0, y1 - y0)), PAPER, (244, 226, 255))
    paste_rounded(img, LABEL, paper, 10)
    d = ImageDraw.Draw(img)
    # the colour stripes every blank tape label had
    stripes = [YELLOW, ORANGE, PINK, PURPLE, CYAN]
    for i, col in enumerate(stripes):
        d.rectangle(sc((x0 + 14, y1 - 50 + i * 6, x1 - 14, y1 - 50 + i * 6 + 5)), fill=col)
    f_small = font(F_GOTHIC, 15)
    draw_text(d, (x0 + 18, y0 + 14), "ＶＨＳ　バイブス", f_small, INK, 2.0)
    draw_text(d, (x1 - 18, y0 + 14), "Ｔ－１２０", f_small, PINK, 1.0, "r")
    gradient_text(img, ((x0 + x1) / 2 - 4, y0 + 36), "VHS Vibes", font(F_BRUSH, 80), (120, 30, 200), PINK,
                  shadow=CYAN, shadow_off=(3, 3), anchor="m")
    d = ImageDraw.Draw(img)
    f_tiny = font(F_GOTHIC, 12)
    draw_text(d, (x0 + 18, y1 - 17), "ａｅｓｔｈｅｔｉｃ　ｔａｐｅ", f_tiny, INK, 0.0)
    draw_text(d, (x1 - 18, y1 - 17), "anthonytheinventor", f_tiny, (150, 110, 195), 0.5, "r")   # the maker's name, small
    d.rounded_rectangle(sc((x0, y0, x1, y1)), sc(10), outline=(214, 190, 235), width=sc(1))


def draw_hub(d, cx, cy, angle, s):
    """The white hub with its six teeth, turned by `angle` degrees."""
    d.ellipse(sc((cx - 32, cy - 32, cx + 32, cy + 32), s), fill=(246, 240, 252, 255), outline=(190, 170, 215, 255), width=sc(1, s))
    d.ellipse(sc((cx - 21, cy - 21, cx + 21, cy + 21), s), fill=(18, 6, 36, 255))
    for k in range(6):
        a = math.radians(k * 60 + 15 + angle)
        pts = []
        for rr, da in ((22, -0.20), (22, 0.20), (11, 0.34), (11, -0.34)):
            pts.append(sc((cx + rr * math.cos(a + da), cy + rr * math.sin(a + da)), s))
        d.polygon(pts, fill=(246, 240, 252, 255))
    d.ellipse(sc((cx - 5, cy - 5, cx + 5, cy + 5), s), fill=(246, 240, 252, 255))


def draw_reel(layer, cx, cy, pack_r, s=S):
    d = ImageDraw.Draw(layer)
    # clear flange
    d.ellipse(sc((cx - 86, cy - 86, cx + 86, cy + 86), s), fill=(255, 255, 255, 16), outline=(255, 255, 255, 60), width=sc(1, s))
    # tape pack with a few sheen rings
    d.ellipse(sc((cx - pack_r, cy - pack_r, cx + pack_r, cy + pack_r), s), fill=(26, 14, 26, 255), outline=(70, 40, 70, 255), width=sc(1, s))
    for r in range(38, int(pack_r), 7):
        d.ellipse(sc((cx - r, cy - r, cx + r, cy + r), s), outline=(52, 30, 54, 255), width=sc(1, s))
    draw_hub(d, cx, cy, 0.0, s)


def make_hub_strip():
    """REEL_FRAMES frames of the hub turning through one tooth spacing (60 degrees)."""
    s = 4
    strip = Image.new("RGBA", (HUB, HUB * REEL_FRAMES), (0, 0, 0, 0))
    for k in range(REEL_FRAMES):
        frame = Image.new("RGBA", (HUB * s, HUB * s), (0, 0, 0, 0))
        draw_hub(ImageDraw.Draw(frame), HUB / 2, HUB / 2, 60.0 * k / REEL_FRAMES, s)
        strip.paste(frame.resize((HUB, HUB), Image.LANCZOS), (0, k * HUB))
    return strip


def draw_window(img):
    x0, y0, x1, y1 = WINDOW
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(sc((x0 - 5, y0 - 5, x1 + 5, y1 + 5)), sc(20), fill=PINK_LO)
    d.rounded_rectangle(sc((x0 - 5, y0 - 5, x1 + 5, y1 + 5)), sc(20), outline=(160, 10, 90), width=sc(1))
    glass = gradient(sc((x1 - x0, y1 - y0)), (18, 6, 38), (52, 20, 92)).convert("RGBA")
    reels = Image.new("RGBA", glass.size, (0, 0, 0, 0))
    cy = (y1 - y0) / 2
    draw_reel(reels, 100, cy, 78)
    draw_reel(reels, 300, cy, 48)
    # tape path between the packs along the bottom
    rd = ImageDraw.Draw(reels)
    hh = y1 - y0
    path = [(100 - 62, cy + 47), (24, hh - 12), (376, hh - 12), (300 + 40, cy + 26)]
    rd.line([sc(pt) for pt in path], fill=(40, 22, 40, 255), width=sc(2), joint="curve")
    for gx in (24, 376):
        rd.ellipse(sc((gx - 5, hh - 17, gx + 5, hh - 7)), fill=(210, 200, 225, 255))
    glass.alpha_composite(reels)
    # glare
    glare = Image.new("RGBA", glass.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glare)
    gd.polygon([sc(p) for p in ((40, 0), (120, 0), (30, y1 - y0), (-50, y1 - y0))], fill=(255, 255, 255, 20))
    gd.polygon([sc(p) for p in ((150, 0), (175, 0), (85, y1 - y0), (60, y1 - y0))], fill=(255, 255, 255, 14))
    glass.alpha_composite(glare)
    paste_rounded(img, WINDOW, glass.convert("RGB"), 15)
    d.rounded_rectangle(sc((x0, y0, x1, y1)), sc(15), outline=(120, 80, 190), width=sc(1))


def sunset(phase, level, colour=0, bend=0.0):
    """The striped sun over a neon grid, sticker-sized, supersampled.
    phase 0..1 scrolls the grid by one line spacing; level 0..1 lights the sun; colour picks the
    sun's colours (SUN_COLOURS); bend swings the grid's long lines into a wave that travels with the
    scroll (px at the bottom edge, 0: straight)."""
    top_c, bot_c, halo_c, top_hot, bot_hot = SUN_COLOURS[colour]
    w, h = STICKER_W, STICKER_H
    horizon = HORIZON
    art = gradient(sc((w, h)), (22, 6, 58), (150, 40, 150))
    d = ImageDraw.Draw(art)
    rnd = random.Random(7)
    for _ in range(26):
        sx, sy = rnd.uniform(4, w - 4), rnd.uniform(4, horizon - 30)
        d.ellipse(sc((sx, sy, sx + 1.4, sy + 1.4)), fill=(255, 255, 255))
    r = int(h * 0.36)
    cx, cy = int(w * 0.62), horizon - 4
    # halo: grows and brightens with the level
    if level > 0.0:
        halo = Image.new("RGBA", art.size, (0, 0, 0, 0))
        rr = r + 3 + 13 * level
        ImageDraw.Draw(halo).ellipse(sc((cx - rr, cy - rr, cx + rr, cy + rr)), fill=halo_c + (int(60 + 170 * level),))
        halo = halo.filter(ImageFilter.GaussianBlur(sc(3 + 5 * level)))
        a = art.convert("RGBA")
        a.alpha_composite(halo)
        art = a.convert("RGB")
        d = ImageDraw.Draw(art)
    # sun: dim when quiet, hot when loud
    bright = 0.5 + 0.5 * level
    top = lerp_col(tuple(int(c * bright) for c in top_c), top_hot, 0.35 * level * level)
    bot = lerp_col(tuple(int(c * bright) for c in bot_c), bot_hot, 0.25 * level * level)
    sun = gradient(sc((2 * r, 2 * r)), top, bot)
    sm = Image.new("L", sun.size, 0)
    md = ImageDraw.Draw(sm)
    md.ellipse((0, 0, sun.size[0] - 1, sun.size[1] - 1), fill=255)
    for k in range(5):                         # the cuts, thicker towards the horizon
        yy = r * (0.95 + 0.21 * k)
        md.rectangle(sc((0, yy, 2 * r, yy + 2.5 + 1.6 * k)), fill=0)
    art.paste(sun, sc((cx - r, cy - r)), sm)
    # ground and grid
    d = ImageDraw.Draw(art)
    d.rectangle(sc((0, horizon, w, h)), fill=(16, 4, 44))
    grid = Image.new("RGBA", art.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(grid)
    for k in range(0, 7):
        yy = horizon + (h - horizon) * ((k + phase) / 6.0) ** 1.8
        if horizon + 1 < yy < h:
            gd.line(sc((0, yy, w, yy)), fill=(0, 240, 255, 230), width=sc(1))
    for k in range(-14, 15):
        if bend == 0.0:
            gd.line(sc((cx + k * 9, horizon, cx + k * 62, h)), fill=(255, 46, 151, 230), width=sc(1))
            continue
        # the wave grows towards the viewer and moves down the line as the grid scrolls, one
        # wavelength per 1 / 1.6 of the depth: at phase 1 it is where it was at 0, as the grid is
        pts = []
        for j in range(33):
            u = j / 32.0
            dx = bend * (0.25 + u) * math.sin(2.0 * math.pi * (1.6 * u - phase))
            pts.append(sc((cx + k * 9 + (k * 62 - k * 9) * u + dx, horizon + (h - horizon) * u)))
        gd.line(pts, fill=(255, 46, 151, 230), width=sc(1), joint="curve")
    gd.line(sc((0, horizon, w, horizon)), fill=(0, 240, 255, 255), width=sc(1.5))
    crop = Image.new("L", art.size, 0)
    ImageDraw.Draw(crop).rectangle(sc((0, horizon, w, h)), fill=255)
    grid.putalpha(ImageChops.multiply(grid.getchannel("A"), crop))
    a = art.convert("RGBA")
    a.alpha_composite(grid.filter(ImageFilter.GaussianBlur(sc(1.5))))
    a.alpha_composite(grid)
    return a.convert("RGB")


def sticker_art(phase=0.0, level=0.0, colour=0, bend=0.0):
    """The page sticker without its words (sunset, border, rounded corners) at final size, RGBA."""
    art = sunset(phase, level, colour, bend)
    ImageDraw.Draw(art).rounded_rectangle(sc((0, 0, STICKER_W, STICKER_H)), sc(10), outline=PAPER, width=sc(3))
    out = art.convert("RGBA")
    out.putalpha(rounded_mask(art.size, sc(10)))
    return out.resize((STICKER_W, STICKER_H), Image.LANCZOS)


def draw_words(art, page, number):
    """The page's name, its katakana and its number, on the supersampled sticker."""
    gradient_text(art, (12, 6), page["name"], font(F_TIMES, 50), (255, 255, 255), (150, 240, 255),
                  shadow=PINK, shadow_off=(3, 3))
    d = ImageDraw.Draw(art)
    draw_text(d, (20, 70), page["jp"], font(F_GOTHIC, 17), CYAN, 3.0)
    draw_text(d, (STICKER_W - 14, 12), "%02d／%02d" % (number, len(PAGES)), font(F_GOTHIC, 14), PAPER, 1.0, "r")


_words = {}


def words_layer(page, number):
    """The words alone, at final size, RGBA: drawn once on black and once on white, the difference
    is their coverage (and on black, their colour times it), so the edges blend over any sun."""
    if number not in _words:
        on = [Image.new("RGB", sc((STICKER_W, STICKER_H)), c) for c in ((0, 0, 0), (255, 255, 255))]
        for im in on:
            draw_words(im, page, number)
        cover = ImageChops.invert(ImageChops.subtract(on[1], on[0]).convert("L"))
        layer = Image.merge("RGBa", on[0].split() + (cover,))
        _words[number] = layer.resize((STICKER_W, STICKER_H), Image.LANCZOS).convert("RGBA")
    return _words[number]


def sticker_image(page, number, phase=0.0, level=0.0):
    """The finished page sticker at final size, RGBA: its picture with its words on top. The
    background and every film strip are cut from the same pictures, so they line up exactly."""
    out = sticker_art(phase, level)
    out.alpha_composite(words_layer(page, number))
    return out


def stack(frames):
    """Pictures of one width (frames, or strips of them), top to bottom: a film strip."""
    strip = Image.new("RGBA", (frames[0].width, sum(fr.height for fr in frames)), (0, 0, 0, 0))
    y = 0
    for fr in frames:
        strip.paste(fr, (0, y))
        y += fr.height
    return strip


def make_grid_strip():
    """The ground with the grid lines rolling towards the viewer: REEL_FRAMES frames straight, then
    as many again for each wobble step, its long lines bent (Grid = step x REEL_FRAMES + Reel's frame).
    The bent ones are cut down to 256 colours (still RGBA; a channel moves by 14 at most), which makes
    them under half the size; the straight ones stay exactly as the grid has always been."""
    straight = stack([sticker_art(k / REEL_FRAMES, 0.0).crop(GRID_BOX) for k in range(REEL_FRAMES)])
    bent = stack([sticker_art(k / REEL_FRAMES, 0.0, 0, bend).crop(GRID_BOX)
                  for bend in GRID_BEND[1:1 + FR["wobble_steps"]] for k in range(REEL_FRAMES)])
    bent = bent.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).convert("RGBA")
    return stack([straight, bent])


def make_sun_strip():
    """The sun from dim to blazing, LEVEL_FRAMES frames in each of its colours (Level = colour x
    LEVEL_FRAMES + level). Without the words: those are laid over it (title_file)."""
    return stack([sticker_art(0.0, k / (LEVEL_FRAMES - 1), c).crop(SUN_BOX)
                  for c in range(FR["colours"]) for k in range(LEVEL_FRAMES)])


def title_box(page, number):
    """Where the words cross the sun's strip: the part of words_layer laid over it."""
    bb = words_layer(page, number).getchannel("A").getbbox()
    return (max(bb[0], SUN_BOX[0]), max(bb[1], SUN_BOX[1]), min(bb[2], SUN_BOX[2]), min(bb[3], SUN_BOX[3]))


def title_file(page):
    return "title_%s.png" % page["name"].lower()


# --------------------------------------------------------------------------------------------
# The picture effects (1.7): transparent strips over the sticker's picture, frame 0 empty
# --------------------------------------------------------------------------------------------
_inside = []


def inside_mask():
    """The picture inside the paper border, sticker-sized (L): the effects are cut to it."""
    if not _inside:
        m = Image.new("L", sc((STICKER_W, STICKER_H)), 0)
        ImageDraw.Draw(m).rounded_rectangle(sc(FX_BOX), sc(7), fill=255)
        _inside.append(m.resize((STICKER_W, STICKER_H), Image.LANCZOS))
    return _inside[0]


def fx_cut(layer, box):
    """A sticker-sized effect layer, kept inside the border, cut to its box."""
    layer.putalpha(ImageChops.multiply(layer.getchannel("A"), inside_mask()))
    return layer.crop(box)


def noise(rng, size, levels=4):
    """Random greys in `levels` steps (few values: the strips compress far better)."""
    step = 256 // levels
    return Image.frombytes("L", size, rng.randbytes(size[0] * size[1])).point(lambda v: min(255, (v // step) * 255 // (levels - 1)))


def tint(grey, r=0.92, g=0.85):
    """A grey picture tinted lavender, as the snow on the mock-ups."""
    return Image.merge("RGB", (grey.point(lambda v: int(v * r)), grey.point(lambda v: int(v * g)), grey))


def snow_layer(s, rng, v=0):
    """Snow at strength s: a fine grain, specks of white, and the stars turned to static (stronger in the sky).
    Variant v sets what tells the patterns apart at a glance: the grain fine or coarse, and a few brighter
    bands of noise (video noise bars) at their own heights, some torn sideways."""
    size = (STICKER_W, STICKER_H)
    sky = Image.new("L", size, 255)
    ImageDraw.Draw(sky).rectangle((0, HORIZON, STICKER_W, STICKER_H), fill=110)
    g = 2 if v % 2 else 1
    layer = tint(noise(rng, (STICKER_W // g, STICKER_H // g)).resize(size, Image.NEAREST)).convert("RGBA")
    alpha = sky.point(lambda a: int(a * (0.05 + 0.20 * s)))
    ad = ImageDraw.Draw(alpha)
    for _ in range(v % 3 + 1 + (s > 0.5)):
        y = rng.randrange(4, STICKER_H - 8)
        ad.rectangle((0, y, STICKER_W, y + rng.randint(1, 3 + int(4 * s))), fill=int(255 * (0.25 + 0.45 * s)))
    layer.putalpha(alpha)
    if v >= 3:   # a torn stretch: a band of the grain shifted sideways
        y = rng.randrange(6, STICKER_H - 16)
        h = rng.randint(4, 10)
        band = layer.crop((0, y, STICKER_W, y + h))
        layer.paste(band, (rng.randint(8, 40) * rng.choice((-1, 1)), y))
    specks = Image.frombytes("L", size, rng.randbytes(size[0] * size[1])).point(lambda v: 255 if v < (0.006 + 0.075 * s) * 255 else 0)
    spk = tint(noise(rng, size, 3).point(lambda v: 150 + v * 105 // 255), 0.95, 0.9).convert("RGBA")
    spk.putalpha(ImageChops.multiply(specks, sky))
    layer.alpha_composite(spk)
    d = ImageDraw.Draw(layer)
    cols = [(255, 255, 255), (255, 255, 255), CYAN, PINK_HI, YELLOW, PURPLE]
    for _ in range(int(15 + 320 * s)):
        x, y = rng.randrange(4, STICKER_W - 4), rng.randrange(4, HORIZON - 3)
        c = rng.choice(cols) + (255,)
        d.point([(x, y), (x + 1, y)] if rng.random() < 0.25 else [(x, y)], fill=c)
    return fx_cut(layer, FX_BOX)


def head_layer(s, rng):
    """The head switch strip at strength s: the bottom rows torn sideways, noisy and pale, with a
    ragged bright edge where the heads hand over. The torn rows are the picture itself (at a random
    moment of the grid's scroll), as on the mock-ups."""
    pic = sticker_art(rng.randrange(REEL_FRAMES) / REEL_FRAMES, 0.0).convert("RGB")
    n = int(4 + 11 * s) + rng.randint(-1, 2)
    y0 = STICKER_H - 3 - n
    base = rng.uniform(0.6, 1.4)
    for i, y in enumerate(range(y0, STICKER_H - 3)):
        dx = int(round((2 + (i + 1) / n * (6 + 26 * s)) * base * rng.uniform(0.75, 1.25)))
        if rng.random() < 0.15:
            dx = -dx // 2
        row = pic.crop((0, y, STICKER_W, y + 1))
        pic.paste((60, 40, 90), (0, y, STICKER_W, y + 1))
        pic.paste(row, (dx, y))
    strip = pic.crop((0, y0, STICKER_W, STICKER_H - 3))
    nz = noise(rng, (STICKER_W // 2, STICKER_H - 3 - y0)).resize(strip.size, Image.NEAREST)
    strip = Image.blend(strip, tint(nz, 0.9, 0.85), 0.25 + 0.35 * s)
    strip = Image.blend(strip.convert("L").convert("RGB"), strip, 0.5)   # half the colour
    layer = Image.new("RGBA", (STICKER_W, STICKER_H), (0, 0, 0, 0))
    layer.paste(strip, (0, y0))
    d = ImageDraw.Draw(layer)
    x = rng.randrange(0, 40)
    while x < STICKER_W:
        ln = rng.randrange(6, 50)
        if rng.random() < 0.6:
            d.line((x, y0, x + ln, y0), fill=(235, 225, 255, 255))
        x += ln + rng.randrange(2, 30)
    return fx_cut(layer, HEAD_BOX)


def streaks(d, rng, n, s=0.7):
    """n white streaks fading to the right, here and there a second, shorter one under it."""
    for _ in range(n):
        y = rng.randrange(6, STICKER_H - 6)
        ln = rng.randrange(12, int(30 + 140 * s))
        x0 = rng.randrange(-20, STICKER_W - 10)
        for j in range(0, ln, 3):
            d.line((x0 + j, y, x0 + j + 3, y), fill=(255, 255, 255, int(250 * (1 - j / ln) ** 1.3)))
        if rng.random() < 0.35:
            d.line((x0, y + 1, x0 + ln // 3, y + 1), fill=(255, 255, 255, 140))


def streak_layer(rng):
    """A dropout: two to five white streaks across the picture."""
    layer = Image.new("RGBA", (STICKER_W, STICKER_H), (0, 0, 0, 0))
    streaks(ImageDraw.Draw(layer), rng, rng.randint(2, 5), 0.9)
    return fx_cut(layer, FX_BOX)


def band_layer(pos, rng):
    """The tracking band at height `pos` (0 at the bottom): a band of noise across the picture with
    torn coloured smears in it, densest in its middle, and a streak or two beside it."""
    s = 0.95
    bh = int(8 + 10 * s)
    yc = (STICKER_H - 3) - (pos + 0.5) * (STICKER_H - 6) / FR["bands"]
    y0 = int(yc - bh / 2)
    layer = Image.new("RGBA", (STICKER_W, STICKER_H), (0, 0, 0, 0))
    band = tint(noise(rng, (STICKER_W // 5, bh)).resize((STICKER_W, bh), Image.NEAREST), 0.95, 0.9).convert("RGBA")
    a = Image.new("L", (1, bh))
    a.putdata([int(255 * (0.45 + 0.5 * math.sin(math.pi * (y + 0.5) / bh))) for y in range(bh)])
    band.putalpha(a.resize((STICKER_W, bh)))
    d = ImageDraw.Draw(band)
    for _ in range(rng.randint(3, 6)):   # what is left of the picture, torn sideways
        y = rng.randrange(1, bh - 1)
        x = rng.randrange(-30, STICKER_W - 20)
        d.line((x, y, x + rng.randrange(20, 120), y), fill=rng.choice((PINK, CYAN, PURPLE, YELLOW)) + (200,))
    layer.alpha_composite(band, (0, y0))   # (y0 is 5 at the top height, the band ends 6 px above the bottom at the lowest)
    streaks(ImageDraw.Draw(layer), rng, rng.randint(1, 2))
    return fx_cut(layer, FX_BOX)


def haze_alpha(step):
    return int(255 * (0.10 + 0.07 * step))   # 17 % to 45 %


def haze_layer(step):
    layer = Image.new("RGBA", (STICKER_W, STICKER_H), HAZE + (haze_alpha(step),))
    return fx_cut(layer, FX_BOX)


def empty(box):
    return Image.new("RGBA", (box[2] - box[0], box[3] - box[1]), (0, 0, 0, 0))


def make_fx_strips():
    """The four effect strips (file -> image). Seeded, so a run draws the same pictures."""
    rng = random.Random(1987)
    assert len(SNOW_STRENGTH) == FR["snow_steps"], "SNOW_STRENGTH needs one value per kSnowSteps"
    snow = [empty(FX_BOX)] + [snow_layer(SNOW_STRENGTH[st], rng, v) for st in range(FR["snow_steps"]) for v in range(FR["snow_vars"])]
    head = [empty(HEAD_BOX)] + [head_layer(FX_STRENGTH[st], rng) for st in range(FR["head_steps"]) for _ in range(FR["head_vars"])]
    streak = [empty(FX_BOX)] + [streak_layer(rng) for _ in range(FR["streaks"])] + [band_layer(p, rng) for p in range(FR["bands"])]
    haze = [empty(FX_BOX)] + [haze_layer(k) for k in range(1, FR["haze_steps"] + 1)]
    return {"snow.png": stack(snow), "head.png": stack(head), "streak.png": stack(streak), "haze.png": stack(haze)}


# film strip -> (meter, frames, box inside the sticker), in the order they are laid over the picture
FX_STRIPS = [("haze.png", HAZE_PARAM, "haze", FX_BOX), ("snow.png", SNOW_PARAM, "snow", FX_BOX),
             ("head.png", HEAD_PARAM, "head", HEAD_BOX), ("streak.png", STREAK_PARAM, "streak", FX_BOX)]


def decor(img, box, jp):
    """Fill an empty stretch of the face label: huge faint katakana and a few 80s shapes."""
    x0, y0, x1, y1 = box
    w = x1 - x0
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rnd = random.Random(hash((x0, y0, w)) & 0xffff)
    if w >= 400:
        px = 112
        while px > 40 and text_width(jp, font(F_GOTHIC, px), 6.0) / S > w - 40:
            px -= 4
        f = font(F_GOTHIC, px)
        tw = text_width(jp, f, 6.0) / S
        draw_text(d, (x0 + (w - tw) / 2, y0 + (y1 - y0 - px) / 2 - 4), jp, f, (255, 46, 151, 46), 6.0)
    n = max(2, w // 110)
    for i in range(n):
        cx = x0 + (i + 0.5) * w / n + rnd.uniform(-18, 18)
        cy = rnd.uniform(y0 + 34, y1 - 34)
        kind = (i + int(x0)) % 4
        col = [(0, 240, 255, 150), (255, 249, 91, 150), (185, 103, 255, 170), (255, 46, 151, 170)][(i + int(y0)) % 4]
        if kind == 0:      # triangle
            r = rnd.uniform(14, 22)
            rot = rnd.uniform(0, 2)
            pts = [sc((cx + r * math.cos(rot + k * 2.094), cy + r * math.sin(rot + k * 2.094))) for k in range(3)]
            d.line(pts + [pts[0]], fill=col, width=sc(2), joint="curve")
        elif kind == 1:    # zigzag
            pts = [sc((cx - 30 + k * 12, cy + (6 if k % 2 else -6))) for k in range(6)]
            d.line(pts, fill=col, width=sc(2.5), joint="curve")
        elif kind == 2:    # ring
            r = rnd.uniform(9, 15)
            d.ellipse(sc((cx - r, cy - r, cx + r, cy + r)), outline=col, width=sc(2.5))
        else:              # plus
            d.line(sc((cx - 9, cy, cx + 9, cy)), fill=col, width=sc(3))
            d.line(sc((cx, cy - 9, cx, cy + 9)), fill=col, width=sc(3))
    overlay(img, layer)


def cell_x(block, cell):
    return X0 + (block % 2) * 4 * CELL_W + cell * CELL_W


def block_row(block):
    return ROW_Y[block // 2]


def draw_panel(img, page, params):
    x0, y0, x1, y1 = PANEL
    shadowed_box(img, PANEL, 16)
    panel = gradient(sc((x1 - x0, y1 - y0)), PANEL_TOP, PANEL_BOT)
    paste_rounded(img, PANEL, panel, 16)
    # scanlines
    lines = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ld = ImageDraw.Draw(lines)
    for y in range(y0 + 2, y1 - 2, 4):
        ld.line(sc((x0 + 4, y, x1 - 4, y)), fill=(0, 0, 0, 34), width=sc(1))
    overlay(img, lines)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(sc((x0, y0, x1, y1)), sc(16), outline=(150, 232, 255), width=sc(2))
    d.rounded_rectangle(sc((x0 + 5, y0 + 5, x1 - 5, y1 - 5)), sc(12), outline=(255, 46, 151), width=sc(1))

    f_head = font(F_TIMES, 20)
    f_jp = font(F_GOTHIC, 13)
    f_cap = font(F_GOTHIC, 15)

    # empty stretches get decoration (drawn first, under everything else)
    for row in range(2):
        cells = page["blocks"][row * 2][2] + page["blocks"][row * 2 + 1][2]
        i = 0
        while i < 8:
            if cells[i] is None:
                j = i
                while j < 8 and cells[j] is None:
                    j += 1
                decor(img, (X0 + i * CELL_W + 6, ROW_Y[row] + 26, X0 + j * CELL_W - 6, ROW_Y[row] + ROW_H - 8), page["jp"])
                i = j
            else:
                i += 1
    d = ImageDraw.Draw(img)

    for b, (title, jp, cells) in enumerate(page["blocks"]):
        bx, by = cell_x(b, 0), block_row(b)
        used = [c for c in cells if c is not None]
        if not used:
            continue
        # header: title, katakana, then a fading rule
        hx = bx + 46        # the MPC draws its "Q" marker in the top-left corner of the selected Q-Link group
        if title:
            hx += draw_text(d, (hx, by + 5), title, f_head, (255, 130, 205), 1.0) + 10
            hx += draw_text(d, (hx, by + 11), jp, f_jp, (130, 225, 240), 1.0) + 10
        last_used = max(i for i, c in enumerate(cells) if c is not None)
        end = bx + (last_used + 1) * CELL_W - 10
        # an untitled block to the right continues this one: the rule runs on across it (and the
        # untitled block draws none of its own, nor a divider: see below)
        if continues(page, b + 1):
            rc = page["blocks"][b + 1][2]
            end = cell_x(b + 1, 0) + (max(i for i, c in enumerate(rc) if c is not None) + 1) * CELL_W - 10
        if end > hx + 10 and not continues(page, b):
            rule = gradient(sc((end - hx, 2)), PINK, CYAN, horizontal=True)
            img.paste(rule, sc((hx, by + 17)))
        # cells
        for i, name in enumerate(cells):
            if name is None or name == "<":
                continue
            span = 2 if i + 1 < len(cells) and cells[i + 1] == "<" else 1
            cx = bx + i * CELL_W
            sy = by + SLOT_DY
            if name in MODE_ONLY and name not in CAPTION_OVERRIDE:
                continue
            cap, cap_col = CAPTION_OVERRIDE.get(name, (CAPTIONS.get(name, name.upper()), CAPTION_COLOUR))
            draw_text(d, (cx + span * CELL_W / 2, sy + 4), cap, f_cap, cap_col, 1.5, "m")
            p = params[name]
            if p.is_discrete:
                continue
            # knob seat and value window
            kx, ky = cx + KNOB_POS[0], sy + KNOB_POS[1]
            d.ellipse(sc((kx + 4, ky + 4, kx + KNOB - 4, ky + KNOB - 4)), fill=(17, 5, 44))
            vx, vy, vw, vh = VALUE_BOX
            d.rounded_rectangle(sc((cx + vx, sy + vy, cx + vx + vw, sy + vy + vh)), sc(6), fill=(12, 3, 34), outline=(84, 50, 150), width=sc(1))
    # divider between the left and right blocks
    for row in range(2):
        left_used = any(c is not None for c in page["blocks"][row * 2][2])
        right_used = any(c is not None for c in page["blocks"][row * 2 + 1][2])
        if left_used and right_used and not continues(page, row * 2 + 1):
            xx = X0 + 4 * CELL_W
            d.line(sc((xx, ROW_Y[row] + 30, xx, ROW_Y[row] + ROW_H - 12)), fill=(96, 56, 170), width=sc(1))
    # divider between the rows
    d.line(sc((x0 + 18, ROW_Y[1] - 2, x1 - 18, ROW_Y[1] - 2)), fill=(72, 40, 140), width=sc(1))


def continues(page, b):
    """True if block b is the untitled right half of a titled section (VIBE, NOISE, CHORUS, MIC)."""
    bl = page["blocks"]
    return (b % 2 == 1 and b < len(bl) and not bl[b][0] and bool(bl[b - 1][0])
            and any(c is not None for c in bl[b][2]))


def make_background(page, number, params):
    img = Image.new("RGB", sc((W, H)), NIGHT)
    draw_shell(img)
    draw_title_label(img)
    draw_window(img)
    shadowed_box(img, STICKER, 10)
    draw_panel(img, page, params)
    out = img.resize((W, H), Image.LANCZOS).convert("RGBA")
    out.alpha_composite(sticker_image(page, number), (STICKER[0], STICKER[1]))
    return out.convert("RGB")


# --------------------------------------------------------------------------------------------
# Film strips and buttons
# --------------------------------------------------------------------------------------------
KNOB_SWEEP = (CYAN, PURPLE, PINK)
KNOB_POINTER = (235, 255, 255)


def arc_colour(t, sweep=KNOB_SWEEP):
    """Cyan -> purple -> pink (or a mode's three colours) along the knob's sweep."""
    a, b, c = sweep
    return lerp_col(a, b, t * 2) if t < 0.5 else lerp_col(b, c, (t - 0.5) * 2)


def knob_frame(t, bipolar, sweep=KNOB_SWEEP, pointer=KNOB_POINTER):
    s = 4
    n = KNOB * s
    c = n / 2
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r_arc, thick = 41 * s, 6 * s
    box = (c - r_arc, c - r_arc, c + r_arc, c + r_arc)
    d.arc(box, 135, 405, fill=(58, 32, 112, 255), width=thick)
    lit = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    ld = ImageDraw.Draw(lit)
    a0, a1 = (135.0, 135.0 + 270.0 * t) if not bipolar else sorted((270.0, 135.0 + 270.0 * t))
    if a1 - a0 > 0.5:
        a = a0
        while a < a1:
            b = min(a + 3.0, a1)
            ld.arc(box, a, b + 0.6, fill=arc_colour((a - 135.0) / 270.0, sweep) + (255,), width=thick)
            a = b
        for ang in (a0, a1):       # round ends
            rr = r_arc - thick / 2
            ex, ey = c + rr * math.cos(math.radians(ang)), c + rr * math.sin(math.radians(ang))
            col = arc_colour((ang - 135.0) / 270.0, sweep) + (255,)
            ld.ellipse((ex - thick / 2, ey - thick / 2, ex + thick / 2, ey + thick / 2), fill=col)
    glow = lit.filter(ImageFilter.GaussianBlur(3 * s))
    img.alpha_composite(glow)
    img.alpha_composite(lit)
    # cap
    r_cap = 30 * s
    cap = gradient((2 * r_cap, 2 * r_cap), (86, 44, 150), (30, 12, 70)).convert("RGBA")
    m = Image.new("L", cap.size, 0)
    ImageDraw.Draw(m).ellipse((0, 0, cap.size[0] - 1, cap.size[1] - 1), fill=255)
    img.paste(cap, (int(c - r_cap), int(c - r_cap)), m)
    d = ImageDraw.Draw(img)
    d.ellipse((c - r_cap, c - r_cap, c + r_cap, c + r_cap), outline=(190, 120, 255, 255), width=int(1.5 * s))
    d.arc((c - r_cap + 3 * s, c - r_cap + 3 * s, c + r_cap - 3 * s, c + r_cap - 3 * s), 205, 335, fill=(255, 255, 255, 110), width=s)
    # pointer
    ang = math.radians(135.0 + 270.0 * t)
    x0, y0 = c + 10 * s * math.cos(ang), c + 10 * s * math.sin(ang)
    x1, y1 = c + 25 * s * math.cos(ang), c + 25 * s * math.sin(ang)
    d.line((x0, y0, x1, y1), fill=pointer + (255,), width=4 * s)
    for px, py in ((x0, y0), (x1, y1)):
        d.ellipse((px - 2 * s, py - 2 * s, px + 2 * s, py + 2 * s), fill=pointer + (255,))
    return img.resize((KNOB, KNOB), Image.LANCZOS)


def make_knob_strip(bipolar, mode=None):
    look = (mode["sweep"], mode["pointer"]) if mode else (KNOB_SWEEP, KNOB_POINTER)
    strip = Image.new("RGBA", (KNOB, KNOB * KNOB_FRAMES), (0, 0, 0, 0))
    for i in range(KNOB_FRAMES):
        strip.paste(knob_frame(i / (KNOB_FRAMES - 1), bipolar, *look), (0, i * KNOB))
    return strip


def make_toggle(on):
    s = 4
    w, h = TOGGLE_SIZE
    img = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    body = (2 * s, 4 * s, (w - 2) * s, (h - 4) * s)
    r = (h - 8) * s // 2
    if on:
        fill = gradient((body[2] - body[0], body[3] - body[1]), PINK, PURPLE, horizontal=True).convert("RGBA")
        m = Image.new("L", fill.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, fill.size[0] - 1, fill.size[1] - 1), r, fill=255)
        glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(glow).rounded_rectangle(body, r, fill=(255, 46, 151, 150))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(2 * s)))
        img.paste(fill, (body[0], body[1]), m)
    d = ImageDraw.Draw(img)
    if on:
        d.rounded_rectangle(body, r, outline=(255, 210, 240, 255), width=s)
        kx = body[2] - r
        d.ellipse((kx - r + 4 * s, body[1] + 4 * s, kx + r - 4 * s, body[3] - 4 * s), fill=(255, 255, 255, 255))
        draw_text_s(d, (body[0] + 11 * s, body[1] + 10 * s), "ON", font(F_GOTHIC, 15, s), (255, 255, 255, 255), s)
    else:
        d.rounded_rectangle(body, r, fill=(24, 9, 58, 255), outline=(96, 62, 160, 255), width=s)
        kx = body[0] + r
        d.ellipse((kx - r + 4 * s, body[1] + 4 * s, kx + r - 4 * s, body[3] - 4 * s), fill=(120, 96, 176, 255))
        draw_text_s(d, (body[0] + 41 * s, body[1] + 10 * s), "OFF", font(F_GOTHIC, 15, s), (140, 116, 196, 255), s)
    return img.resize((w, h), Image.LANCZOS)


def draw_text_s(d, xy, text, fnt, fill, s, tracking=1.0, centre_w=None):
    """Tracked text on an s-scaled canvas (xy already scaled). centre_w centres it in that width."""
    w = sum(fnt.getlength(ch) for ch in text) + tracking * s * max(0, len(text) - 1)
    x, y = xy
    if centre_w is not None:
        x += (centre_w - w) / 2
    for ch in text:
        d.text((x, y), ch, font=fnt, fill=fill)
        x += fnt.getlength(ch) + tracking * s


def make_segment(size, caption, on, mode=None):
    """One button of a segment switch; a mode's button lights in the mode's colours."""
    s = 4
    w, h = size
    img = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    body = (2 * s, 3 * s, (w - 2) * s, (h - 3) * s)
    r = 8 * s
    c0, c1 = mode["button"] if mode else (PINK, PURPLE)
    if on:
        fill = gradient((body[2] - body[0], body[3] - body[1]), c0, c1, horizontal=True).convert("RGBA")
        m = Image.new("L", fill.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, fill.size[0] - 1, fill.size[1] - 1), r, fill=255)
        glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(glow).rounded_rectangle(body, r, fill=c0 + (140,))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(2 * s)))
        img.paste(fill, (body[0], body[1]), m)
    d = ImageDraw.Draw(img)
    f = font(F_GOTHIC, 14, s)
    if on:
        d.rounded_rectangle(body, r, outline=(mode["button_edge"] if mode else (255, 210, 240)) + (255,), width=s)
        col = (mode["button_text"] if mode else (255, 255, 255)) + (255,)
    else:
        d.rounded_rectangle(body, r, fill=(24, 9, 58, 255), outline=(96, 62, 160, 255), width=s)
        col = (150, 128, 204, 255)
    draw_text_s(d, (0, (h * s - 15 * s) / 2), caption, f, col, s, 1.0, centre_w=w * s)
    return img.resize((w, h), Image.LANCZOS)


def seg_file(name, index, on):
    return "seg_%s_%d_%s.png" % (name.lower().replace(" ", ""), index, "on" if on else "off")


# --------------------------------------------------------------------------------------------
# TUI.json / Q-Links.json
# --------------------------------------------------------------------------------------------
def label_data(label_type, colour, height, justification, font_name="Nexa", style="Bold"):
    return G.obj(("version", 1),
                 ("textStyle", G.obj(("version", 1),
                                     ("font", G.obj(("version", 1), ("name", font_name), ("style", style), ("height", float(height)))),
                                     ("colour", colour), ("justification", justification), ("case", "Original"))),
                 ("type", label_type), ("handleName", "Data"))


def focus_entry(width):
    data = G.obj(("version", 1), ("backgroundColour", "1400f0ff"), ("outlineColour", ARGB_CYAN),
                 ("backgroundInset", 2.0), ("outlineThickness", 2.0))
    return G.entry("focus", "Focus", data, "0 0 %d %d" % (width, SLOT_H), "No", "Show", "WhenFocussed")


def knob_def(bipolar, runs=None, steps=0, switch=None):
    """A knob slot. With mode runs (see knob_runs) it holds one knob picture per run, each shown only
    while the mode switch (handle `switch`, `steps` steps) is on that run's steps."""
    vx, vy, vw, vh = VALUE_BOX
    knob_bounds = "%d %d %d %d" % (KNOB_POS[0], KNOB_POS[1], KNOB, KNOB)
    if runs is None or len(runs) == 1:
        knobs = [G.entry("knob", "Knob", G.knob_filmstrip_data(knob_file(bipolar), KNOB_FRAMES),
                         knob_bounds, "No", "Show", "Always")]
    else:
        knobs = [G.entry("knob " + (m["key"] if m else "plain"), "Knob", G.knob_filmstrip_data(knob_file(bipolar, m), KNOB_FRAMES),
                         knob_bounds, "No", "Show", "Always", invalidating=[step_handle(a, b, steps, switch)])
                 for a, b, m in runs]
    children = [focus_entry(CELL_W)] + knobs + [
        G.entry("value", "Label", label_data("Value", ARGB_CYAN, 20.0, "horizontallyCentred verticallyCentred"),
                "%d %d %d %d" % (vx, vy, vw, vh), "No", "Show", "Always"),
    ]
    return G.component_definition(G.control_actions("knob overlay bipolar" if bipolar else "knob overlay"), children, "00000000")


def toggle_def():
    children = [
        focus_entry(CELL_W),
        G.entry("toggle", "Button", G.button_data("toggle_on.png", "toggle_off.png", 1, 1),
                "%d %d %d %d" % (TOGGLE_POS + TOGGLE_SIZE), "No", "Show", "Always"),
    ]
    return G.component_definition(G.control_actions("menu overlay", double_click=False), children, "00000000")


def segment_def(name, span):
    size, offsets, captions = SEGMENTS[name]
    children = [focus_entry(CELL_W * span)]
    for i, (dx, dy) in enumerate(offsets):
        children.append(G.entry("seg %d" % i, "Button",
                                G.button_data(seg_file(name, i, True), seg_file(name, i, False), i, len(offsets)),
                                "%d %d %d %d" % (dx, dy, size[0], size[1]), "No", "Show", "Always"))
    return G.component_definition(G.control_actions("menu overlay", double_click=False), children, "00000000")


def film_entry(name, image, frames, handle, bounds):
    """A picture whose frame follows a parameter (not touchable)."""
    data = G.obj(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", image),
                 ("numFrames", frames), ("handleName", handle))
    return G.entry(name, "Image", data, "%d %d %d %d" % bounds, "No", "Show", "Always")


def sticker_bounds(box):
    """A box inside the sticker as page bounds (x, y, w, h)."""
    return (STICKER[0] + box[0], STICKER[1] + box[1], box[2] - box[0], box[3] - box[1])


def animation_entries(page, number, params):
    """The moving parts, in drawing order: reel hubs, grid, sun, the page's words over the sun, the effects."""
    out = []
    for i, (cx, cy) in enumerate(REEL_CENTRES):
        out.append(film_entry("reel %d" % i, "reel_hub.png", REEL_FRAMES, params[REEL_PARAM].handle, (cx - HUB // 2, cy - HUB // 2, HUB, HUB)))
    out.append(film_entry("grid", "grid.png", FR["grid"], params[GRID_PARAM].handle, sticker_bounds(GRID_BOX)))
    out.append(film_entry("sun", "sun.png", FR["sun"], params[LEVEL_PARAM].handle, sticker_bounds(SUN_BOX)))
    words = G.obj(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", title_file(page)))
    out.append(G.entry("title", "Image", words, "%d %d %d %d" % sticker_bounds(title_box(page, number)), "No", "Show", "Always"))
    for fname, meter, key, box in FX_STRIPS:
        out.append(film_entry(key, fname, FR[key], params[meter].handle, sticker_bounds(box)))
    return out


def definition_name(p, steps):
    if p.kind == "bool":
        return "vvToggle"
    if p.kind == "choice":
        return "vvSeg " + p.name
    base = "vvKnobBi" if p.name in BIPOLAR else "vvKnobUni"
    runs = knob_runs(p.name, steps)
    if len(runs) == 1:
        return base
    return base + " " + " ".join((m["key"] if m else "plain") + str(a) for a, b, m in runs)   # e.g. "vvKnobUni plain0 vhs1"


def iter_controls(page):
    """Yields (block, cell, span, name) for every control on a page."""
    for b, (_, _, cells) in enumerate(page["blocks"]):
        for i, name in enumerate(cells):
            if name is None or name == "<":
                continue
            span = 2 if i + 1 < len(cells) and cells[i + 1] == "<" else 1
            yield b, i, span, name


def build_tui(params):
    defs = [("vvKnobUni", knob_def(False)), ("vvKnobBi", knob_def(True)), ("vvToggle", toggle_def())]
    steps, switch = mode_steps(params), params[MODE_PARAM].handle
    referenced = {}
    tabs = []
    for ti, page in enumerate(PAGES):
        bg = G.obj(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", page["file"]))
        children = [G.entry("background", "Image", bg, "0 0 %d %d" % (W, H), "No", "Show", "Always")]
        children += animation_entries(page, ti + 1, params)
        for m in MODES:
            for b, i, span, name in iter_controls(page):
                if name in m["knobs"]:
                    pic = G.obj(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", swap_file(name, m)))
                    children.append(G.entry("caption %s %s" % (name, m["key"]), "Image", pic,
                                            "%d %d %d %d" % (cell_x(b, i), block_row(b) + SLOT_DY, CELL_W * span,
                                                             SLOT_H if name in MODE_ONLY else CAPTION_H),
                                            "No", "Show", "Always",
                                            invalidating=[step_handle(m["step"], m["step"], steps, switch)]))
        groups = []
        for b in range(4):
            if any(c is not None for c in page["blocks"][b][2]):
                groups.append("%d %d %d %d" % (cell_x(b, 0) - 4, block_row(b) + 2, 4 * CELL_W + 8, ROW_H - 4))
            else:
                groups.append("0 0 0 0")
        for b, i, span, name in iter_controls(page):
            p = params[name]
            referenced[p.index] = p
            dname = definition_name(p, steps)
            if not any(k == dname for k, _ in defs):
                if p.kind == "choice":
                    defs.append((dname, segment_def(name, span)))
                else:   # a knob some mode recolours
                    defs.append((dname, knob_def(name in BIPOLAR, knob_runs(name, steps), steps, switch)))
            bounds = "%d %d %d %d" % (cell_x(b, i), block_row(b) + SLOT_DY, CELL_W * span, SLOT_H)
            only = [step_handle(MODE_ONLY[name], MODE_ONLY[name], steps, switch)] if name in MODE_ONLY else None
            children.append(G.entry(p.name, dname, G.obj(("version", 1), ("handleName", "Data")), bounds,
                                    "Yes", "Hide", "Always", remap=[("Data", p.handle)], invalidating=only))
        tabs.append(G.obj(("version", 1), ("tabName", page["name"]), ("fnKeyIndex", ti), ("fnKeySubIndex", 0),
                          ("componentDefinition", G.component_definition([], children, ARGB_BG)),
                          ("qlinkBoundsData", groups)))
    defs.append(("knob overlay", G.overlay_component("KnobOverlay", G.knob_overlay_data(False), ARGB_OVERLAY)))
    defs.append(("knob overlay bipolar", G.overlay_component("KnobOverlay", G.knob_overlay_data(True), ARGB_OVERLAY)))
    defs.append(("menu overlay", G.overlay_component("MenuOverlay", G.menu_overlay_data(), ARGB_OVERLAY)))
    replacements = [G.obj(("key", p.handle), ("value", list(p.choices)))
                    for _, p in sorted(referenced.items()) if p.is_discrete]
    page_data = G.obj(
        ("version", 1),
        ("componentDefinitions", G.obj(("version", 2), ("importFiles", []),
                                       ("localComponentDefinitions", [G.obj(("key", k), ("value", v)) for k, v in defs]))),
        ("replacementStrings", replacements),
        ("info", G.obj(("version", 1), ("type", "CompleteDescription"))),
        ("tabs", tabs))
    return G.obj(("pageData", page_data))


def build_qlinks(params):
    """Blocks are rows of four. With "RowTopDown" the banks are Q-Links 13-16, 9-12, 5-8, 1-4,
    which are our blocks in reading order (top-left, top-right, bottom-left, bottom-right)."""
    first = [13, 9, 5, 1]
    entries = []
    for ti, page in enumerate(PAGES):
        assigned = {}
        for b, i, _, name in iter_controls(page):
            assigned[first[b] + i] = params[name].index
        qmap = G.Obj(("Q-Link %d" % q, assigned.get(q, -1)) for q in range(1, 17))
        entries.append(G.obj(("Tab", ti + 1), ("SubTab", 1), ("Bank Direction", "RowTopDown"), ("Q-Links", qmap)))
    program = G.Obj(("Q-Link %d" % (q + 1), params[n].index) for q, n in enumerate(PROGRAM_QLINKS))
    return G.obj(("version", 4),
                 ("info", G.obj(("version", 1), ("type", "CompleteDescription"))),
                 ("Screen Mode Q-Links", G.obj(("version", 4), ("map", entries))),
                 ("Program Mode Q-Links", program))


# --------------------------------------------------------------------------------------------
# Preview: what a page will roughly look like on the MPC (values at their defaults)
# --------------------------------------------------------------------------------------------
PREVIEW_FX = {"grid": 9, "sun": 12}   # the moving parts frozen at a lively moment; the effects at frame 0


def frame_of(strip, k, frames):
    h = strip.height // frames
    return strip.crop((0, k * h, strip.width, (k + 1) * h))


def make_preview(page, number, bg, params, images, step=0, displays=None, fx=None, values=None):
    """`step` sets MODE_PARAM (and so the knob colours and captions); every other value is its default
    unless `values` (name -> position 0..1) says otherwise. The read-outs are params.json's (taken with
    every parameter at its default) unless `displays` (name -> text) says otherwise. `fx` (name -> frame:
    grid, sun, snow, head, streak, haze) sets the moving parts, as the plugin's meters would."""
    img = bg.convert("RGBA")
    steps = mode_steps(params)
    mode = mode_at(step)
    fx = dict(PREVIEW_FX, **(fx or {}))
    if mode:
        for b, i, span, name in iter_controls(page):
            if name in mode["knobs"]:
                img.alpha_composite(images[swap_file(name, mode)].convert("RGBA"), (cell_x(b, i), block_row(b) + SLOT_DY))
    for cx, cy in REEL_CENTRES:
        img.alpha_composite(frame_of(images["reel_hub.png"], 7, REEL_FRAMES), (cx - HUB // 2, cy - HUB // 2))
    img.alpha_composite(frame_of(images["grid.png"], fx["grid"], FR["grid"]), sticker_bounds(GRID_BOX)[:2])
    img.alpha_composite(frame_of(images["sun.png"], fx["sun"], FR["sun"]), sticker_bounds(SUN_BOX)[:2])
    img.alpha_composite(images[title_file(page)], sticker_bounds(title_box(page, number))[:2])
    for fname, _, key, box in FX_STRIPS:
        img.alpha_composite(frame_of(images[fname], fx.get(key, 0), FR[key]), sticker_bounds(box)[:2])
    d = ImageDraw.Draw(img)
    try:
        vf = ImageFont.truetype(os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\Windows\Fonts\Roboto-Bold.ttf"), 17)
    except OSError:
        vf = ImageFont.truetype(FONTS + "arialbd.ttf", 16)
    for b, i, span, name in iter_controls(page):
        if name in MODE_ONLY and MODE_ONLY[name] != step:
            continue
        p = params[name]
        x, y = cell_x(b, i), block_row(b) + SLOT_DY
        t = p.default if p.default is not None else 0.0
        if name == MODE_PARAM:
            t = step / (steps - 1)
        elif values and name in values:
            t = values[name]
        if p.kind == "bool":
            img.alpha_composite(images["toggle_on.png" if t >= 0.5 else "toggle_off.png"], (x + TOGGLE_POS[0], y + TOGGLE_POS[1]))
        elif p.kind == "choice":
            size, offsets, _ = SEGMENTS[name]
            cur = int(round(t * (len(offsets) - 1)))
            for k, (dx, dy) in enumerate(offsets):
                img.alpha_composite(images[seg_file(name, k, k == cur)], (x + dx, y + dy))
        else:
            strip = images[knob_file(name in BIPOLAR, mode if mode and name in mode["knobs"] else None)]
            frame = int(round(t * (KNOB_FRAMES - 1)))
            img.alpha_composite(strip.crop((0, frame * KNOB, KNOB, (frame + 1) * KNOB)), (x + KNOB_POS[0], y + KNOB_POS[1]))
            vx, vy, vw, vh = VALUE_BOX
            text = (displays or {}).get(name, p.display)
            tw = d.textlength(text, font=vf)
            d.text((x + vx + (vw - tw) / 2, y + vy + 3), text, font=vf, fill=CYAN + (255,))
    return img.convert("RGB")


def fx_previews(page, number, bg, params, images):
    """The picture effects on a page (build/preview/fx_*.png), and fx_sheet.png: the stickers alone, 3x."""
    V = FR["snow_vars"]
    heavy, mid = 1 + (FR["snow_steps"] - 1) * V, 1 + V  # the first variant of the top and the second step
    hicut = math.log(1400.0 / 1000.0) / math.log(18.0)  # Hi Cut at 1.4 kHz (its knob runs 1 to 18 kHz, log)
    shots = [
        ("clean", "clean (or Visual Vibes off)", {}, 0, None, None),
        ("hiss", "heavy hiss", {"snow": heavy}, 0, None, None),
        ("head", "Head Buzz high", {"head": 1 + 2 * FR["head_vars"]}, 0, None, None),
        ("dropout", "a dropout", {"streak": 1}, 0, None, None),
        ("tracking", "tracking lost: the band", {"streak": 1 + FR["streaks"] + 3}, 0, None, None),
        ("wobble", "wobble high", {"grid": FR["wobble_steps"] * REEL_FRAMES + 9}, 0, None, None),
        ("hicut", "Hi Cut 1.4 kHz", {"haze": FR["haze_steps"]}, 0, {"Hi Cut": hicut}, {"Hi Cut": "1.4 kHz"}),
        ("sun_vhs", "VHS: mint sun", {"sun": LEVEL_FRAMES + 12}, 1, None, None),
        ("sun_hifi", "Hi-Fi: orange sun", {"sun": 2 * LEVEL_FRAMES + 12}, 2, None, None),
        ("worn", "worn tape, VHS", {"snow": mid + 1, "head": 1 + FR["head_vars"] + 2, "streak": 2,
                                    "grid": 2 * REEL_FRAMES + 9, "haze": 2, "sun": LEVEL_FRAMES + 12}, 1, None, None),
    ]
    stickers = []
    for key, label, fx, step, values, displays in shots:
        dfile = os.path.join(PROJECT, "build", "displays_%s.json" % (mode_at(step) or {}).get("key", ""))
        if displays is None and os.path.isfile(dfile):   # the mode's read-outs, as for the pages below
            with open(dfile, "r", encoding="utf-8") as fh:
                displays = json.load(fh)
        pic = make_preview(page, number, bg, params, images, step, displays, fx, values)
        pic.save(os.path.join(PREVIEW, "fx_%s.png" % key))
        stickers.append((label, pic.crop(STICKER).resize((STICKER_W * 3, STICKER_H * 3), Image.NEAREST)))
    cols, gap, lh = 2, 24, 30
    sheet = Image.new("RGB", (cols * (STICKER_W * 3 + gap) + gap, (len(stickers) + cols - 1) // cols * (STICKER_H * 3 + lh + gap) + gap), (18, 10, 30))
    d = ImageDraw.Draw(sheet)
    for i, (label, im) in enumerate(stickers):
        x = gap + (i % cols) * (STICKER_W * 3 + gap)
        y = gap + (i // cols) * (STICKER_H * 3 + lh + gap)
        d.text((x, y), label, font=ImageFont.truetype(FONTS + "arialbd.ttf", 20), fill=(235, 230, 250))
        sheet.paste(im, (x, y + lh))
    sheet.save(os.path.join(PREVIEW, "fx_sheet.png"))


# --------------------------------------------------------------------------------------------
def main():
    if not os.path.isfile(PARAMS):
        sys.exit("make_skin: %s not found. Run tools/build.py --target native first." % PARAMS)
    plist = G.load_params(PARAMS)
    params = {p.name: p for p in plist}
    for page in PAGES:
        for _, _, _, name in iter_controls(page):
            if name not in params:
                sys.exit('make_skin: page %s uses unknown parameter "%s"' % (page["name"], name))
    shown = {name for page in PAGES for _, _, _, name in iter_controls(page)}
    for name in METERS:
        if name not in params:
            sys.exit('make_skin: the plugin has no "%s" parameter (needed for the animation)' % name)
    missing = [p.name for p in plist if p.name not in shown and p.name not in METERS]
    if missing:
        print("make_skin: NOTE: parameters not on any page: " + ", ".join(missing))

    os.makedirs(SKINS, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    for f in os.listdir(SKINS):
        if f.lower().endswith((".png", ".json")):
            os.remove(os.path.join(SKINS, f))

    steps = mode_steps(params)
    if len(SEGMENTS[MODE_PARAM][1]) != steps:
        sys.exit('make_skin: "%s" has %d choices but %d buttons in SEGMENTS' % (MODE_PARAM, steps, len(SEGMENTS[MODE_PARAM][1])))
    for m in MODES:
        for name in m["knobs"]:
            if name not in shown or params[name].is_discrete:
                sys.exit('make_skin: mode %s recolours "%s", which is not a knob on any page' % (m["key"], name))

    images = {}
    images["knob_uni.png"] = make_knob_strip(False)
    images["knob_bi.png"] = make_knob_strip(True)
    for m in MODES:     # one strip per mode and knob kind it recolours
        for bipolar in sorted({name in BIPOLAR for name in m["knobs"]}):
            images[knob_file(bipolar, m)] = make_knob_strip(bipolar, m)
    images["toggle_on.png"] = make_toggle(True)
    images["toggle_off.png"] = make_toggle(False)
    for name, (size, offsets, captions) in SEGMENTS.items():
        for i, cap in enumerate(captions):
            images[seg_file(name, i, True)] = make_segment(size, cap, True, mode_at(i) if name == MODE_PARAM else None)
            images[seg_file(name, i, False)] = make_segment(size, cap, False)
    images["reel_hub.png"] = make_hub_strip()
    images["grid.png"] = make_grid_strip()
    images["sun.png"] = make_sun_strip()
    for n, page in enumerate(PAGES, 1):
        images[title_file(page)] = words_layer(page, n).crop(title_box(page, n))
    images.update(make_fx_strips())
    for fname, im in images.items():
        im.save(os.path.join(SKINS, fname), optimize=True)

    for n, page in enumerate(PAGES, 1):
        bg = make_background(page, n, params)
        bg.save(os.path.join(SKINS, page["file"]), optimize=True)
        # each mode's captions: the same background painted with them, cut out strip by strip
        for m in MODES:
            swaps = [(b, i, span, name) for b, i, span, name in iter_controls(page) if name in m["knobs"]]
            if not swaps:
                continue
            for name, cap in m["knobs"].items():
                CAPTION_OVERRIDE[name] = (cap or CAPTIONS.get(name, name.upper()), m["caption"])
            alt = make_background(page, n, params)
            CAPTION_OVERRIDE.clear()
            for b, i, span, name in swaps:
                x, y = cell_x(b, i), block_row(b) + SLOT_DY
                images[swap_file(name, m)] = alt.crop((x, y, x + CELL_W * span, y + (SLOT_H if name in MODE_ONLY else CAPTION_H)))
                images[swap_file(name, m)].save(os.path.join(SKINS, swap_file(name, m)), optimize=True)
        stem = "page_%d_%s" % (n, page["name"].lower())
        make_preview(page, n, bg, params, images).save(os.path.join(PREVIEW, stem + ".png"))
        if n == 1:
            fx_previews(page, n, bg, params, images)
        # and the page as it looks in each mode that changes something on it. The plugin words some read-outs
        # differently in a mode; build/displays_<key>.json (name -> text), if there, supplies them
        # (write it by asking the native build for its read-outs with the switch on that step).
        on_page = {name for _, _, _, name in iter_controls(page)}
        for m in MODES:
            if MODE_PARAM in on_page or on_page & set(m["knobs"]):
                dfile = os.path.join(PROJECT, "build", "displays_%s.json" % m["key"])
                displays = None
                if os.path.isfile(dfile):
                    with open(dfile, "r", encoding="utf-8") as fh:
                        displays = json.load(fh)
                # (the sun in the mode's colour, as Visual Vibes shows it)
                make_preview(page, n, bg, params, images, m["step"], displays, {"sun": m["step"] * LEVEL_FRAMES + 12}) \
                    .save(os.path.join(PREVIEW, "%s_%s.png" % (stem, m["key"])))
                # and with a knob of the mode's turned up, so its read-out is seen (_up1, _up2, ...)
                ups = [u for u in MODE_SHOWCASE.get(m["key"], []) if u[0] in on_page]
                for k, (name, pos, text) in enumerate(ups, 1):
                    make_preview(page, n, bg, params, images, m["step"], dict(displays or {}, **{name: text}),
                                 {"sun": m["step"] * LEVEL_FRAMES + 12}, {name: pos}) \
                        .save(os.path.join(PREVIEW, "%s_%s_up%d.png" % (stem, m["key"], k)))

    with open(os.path.join(SKINS, "TUI.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(G.dump_json(build_tui(params)) + "\n")
    with open(os.path.join(SKINS, "Q-Links.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(G.dump_json(build_qlinks(params)) + "\n")

    total = sum(os.path.getsize(os.path.join(SKINS, f)) for f in os.listdir(SKINS))
    print("make_skin: wrote %d files to %s (%.0f KB); previews in %s" % (len(os.listdir(SKINS)), SKINS, total / 1024.0, PREVIEW))


if __name__ == "__main__":
    main()
