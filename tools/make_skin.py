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

Moving parts: the plugin has two read-only "meter" parameters, Reel and Level. Film strips
bound to them animate the picture: the two reel hubs and the neon grid follow Reel, the sun
on the page sticker follows Level. REEL_FRAMES and LEVEL_FRAMES must equal kReelFrames and
kLevelFrames in src/VHSVibes.cpp.
"""

import json
import math
import os
import random
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

# Animated parts (see the note at the top).
REEL_FRAMES = 24                   # == kReelFrames: one tooth-to-tooth step of the hub (60 degrees)
LEVEL_FRAMES = 16                  # == kLevelFrames
REEL_PARAM, LEVEL_PARAM = "Reel", "Level"
HUB = 70                           # hub film-strip frame size
REEL_CENTRES = [(WINDOW[0] + 100, (WINDOW[1] + WINDOW[3]) // 2), (WINDOW[0] + 300, (WINDOW[1] + WINDOW[3]) // 2)]
STICKER_W, STICKER_H = STICKER[2] - STICKER[0], STICKER[3] - STICKER[1]
HORIZON = int(STICKER_H * 0.66)    # y of the horizon inside the sticker
SUN_BOX = (96, 16, 326, HORIZON)   # part of the sticker the sun film strip covers
GRID_BOX = (0, HORIZON, STICKER_W, STICKER_H)   # part the grid film strip covers

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
        ("PREAMP", "プリアンプ", ["Drive", "Tone", "Slew", "Comp"]),
        ("OUTPUT", "アウトプット", ["Dropouts", "Mono", "Mix", "Output"]),
        ("SATURATOR", "サチュレーター", ["Saturator", "Split", "Lows", "Lo Sat"]),
        ("", "", ["Highs", "Hi Sat", "Hi Cut", "Sat Type"]),
    ]},
    {"name": "WOBBLE", "jp": "ゆらぎ", "file": "bg_wobble.png", "blocks": [
        ("TRACKING", "トラッキング", ["Warp", "RPM", "Shape", None]),
        ("FLUTTER", "フラッター", ["Flutter", "Flut Rate", "Scrape", "Wobble On"]),
        ("RE-RECORD", "ダビング", ["Wow", "Wow Rate", "Flutter 2", "Flut2 Rate"]),
        ("FILTER", "フィルター", ["Lo Cut", "Hi Cut 2", None, None]),
    ]},
    {"name": "NOISE", "jp": "ノイズ", "file": "bg_noise.png", "blocks": [
        ("NOISE", "ノイズ", ["Hum", "Buzz", "Head Buzz", "Hiss"]),
        ("", "", ["Crackle", "Noise", "Noise Pos", "Noise On"]),
        ("TV SET", "テレビ", ["Whistle", None, None, None]),
        ("", "", [None, None, None, None]),
    ]},
    {"name": "INPUT", "jp": "インプット", "file": "bg_input.png", "blocks": [
        ("CHORUS", "コーラス", ["Chorus", "Cho Mix", "Cho Speed", "Cho Delay"]),
        ("", "", ["Cho Width", None, None, None]),
        ("MIC", "マイク", ["Mic", "Mic Type", "<", "Mic Gain"]),
        ("", "", [None, None, None, None]),
    ]},
    {"name": "DECK", "jp": "デッキ", "file": "bg_deck.png", "blocks": [
        ("DECK", "デッキ", ["Deck On", "AGC", "Drift", None]),
        ("HI-FI TRACK", "ハイファイ", ["Tracking", "Breathing", None, None]),
        ("SCREEN", "スクリーン", ["Animation", None, None, None]),
        ("QUALITY", "高音質", ["HQ", None, "B-Type NR", "<"]),
    ]},
]

# Caption printed above a control (default: the parameter name in capitals).
CAPTIONS = {
    "Saturator": "POWER", "Chorus": "POWER", "Mic": "POWER",
    "Cho Mix": "MIX", "Cho Speed": "SPEED", "Cho Delay": "DELAY", "Cho Width": "WIDTH",
    "Mic Type": "TYPE", "Mic Gain": "GAIN",
    "Flutter": "DEPTH", "Flut Rate": "RATE",
    "Wow Rate": "RATE", "Flutter 2": "FLUTTER", "Flut2 Rate": "RATE",
    "Hi Cut 2": "HI CUT", "Noise": "MASTER", "Noise Pos": "POSITION",
    "Sat Type": "TYPE",
    "Wobble On": "WOBBLE", "Noise On": "NOISE",
    "Deck On": "POWER",
}
BIPOLAR = {"Tone", "Lows", "Mic Gain"}
# The 16 parameters on the knobs in Program (track) Q-Link mode, Q-Link 1..16.
PROGRAM_QLINKS = ["Warp", "Flutter", "Wow", "Flutter 2", "Drive", "Slew", "Comp", "Hi Cut",
                  "Hiss", "Crackle", "Hum", "Noise", "Hi Sat", "Dropouts", "Mix", "Output"]

# Captions that change with a switch: while SWAP_PARAM sits on step SWAP_STEP (of SWAP_STEPS), a small
# picture with the other caption is shown over the one painted on the background. The control under
# it stays the same parameter (and the same Q-Link).
CAPTION_SWAPS = {"Split": "BIAS", "Lo Sat": "REC LEVEL", "Hi Sat": "TAPE SPEED"}
SWAP_PARAM, SWAP_STEP, SWAP_STEPS = "Sat Type", 1, 2
CAPTION_H = 25                     # height of the caption strip at the top of a slot
CAPTION_OVERRIDE = {}              # used while the other captions are being painted


def swap_file(name):
    return "cap_%s_vhs.png" % name.lower().replace(" ", "")


# Segment switches: parameter -> (button size, [(dx, dy) inside the slot], [captions])
SEGMENTS = {
    "Noise Pos": ((64, 40), [(4, 50), (72, 50)], ["POST", "PRE"]),
    "Sat Type": ((64, 40), [(4, 50), (72, 50)], ["2-BAND", "VHS"]),
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


def sunset(phase, level):
    """The striped sun over a neon grid, sticker-sized, supersampled.
    phase 0..1 scrolls the grid by one line spacing; level 0..1 lights the sun."""
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
        ImageDraw.Draw(halo).ellipse(sc((cx - rr, cy - rr, cx + rr, cy + rr)), fill=(255, 120, 200, int(60 + 170 * level)))
        halo = halo.filter(ImageFilter.GaussianBlur(sc(3 + 5 * level)))
        a = art.convert("RGBA")
        a.alpha_composite(halo)
        art = a.convert("RGB")
        d = ImageDraw.Draw(art)
    # sun: dim when quiet, hot when loud
    bright = 0.5 + 0.5 * level
    top = lerp_col(tuple(int(c * bright) for c in YELLOW), (255, 255, 255), 0.35 * level * level)
    bot = lerp_col(tuple(int(c * bright) for c in PINK), (255, 200, 230), 0.25 * level * level)
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
        gd.line(sc((cx + k * 9, horizon, cx + k * 62, h)), fill=(255, 46, 151, 230), width=sc(1))
    gd.line(sc((0, horizon, w, horizon)), fill=(0, 240, 255, 255), width=sc(1.5))
    crop = Image.new("L", art.size, 0)
    ImageDraw.Draw(crop).rectangle(sc((0, horizon, w, h)), fill=255)
    grid.putalpha(ImageChops.multiply(grid.getchannel("A"), crop))
    a = art.convert("RGBA")
    a.alpha_composite(grid.filter(ImageFilter.GaussianBlur(sc(1.5))))
    a.alpha_composite(grid)
    return a.convert("RGB")


def sticker_image(page, number, phase=0.0, level=0.0):
    """The finished page sticker (sunset, page name, border, rounded corners) at final size, RGBA.
    The background and the sun / grid film strips are all cut from this, so they line up exactly."""
    art = sunset(phase, level)
    d = ImageDraw.Draw(art)
    d.rounded_rectangle(sc((0, 0, STICKER_W, STICKER_H)), sc(10), outline=PAPER, width=sc(3))
    gradient_text(art, (12, 6), page["name"], font(F_TIMES, 50), (255, 255, 255), (150, 240, 255),
                  shadow=PINK, shadow_off=(3, 3))
    d = ImageDraw.Draw(art)
    draw_text(d, (20, 70), page["jp"], font(F_GOTHIC, 17), CYAN, 3.0)
    draw_text(d, (STICKER_W - 14, 12), "%02d／%02d" % (number, len(PAGES)), font(F_GOTHIC, 14), PAPER, 1.0, "r")
    out = art.convert("RGBA")
    out.putalpha(rounded_mask(art.size, sc(10)))
    return out.resize((STICKER_W, STICKER_H), Image.LANCZOS)


def make_grid_strip():
    """REEL_FRAMES frames of the ground with the grid lines rolling towards the viewer."""
    x0, y0, x1, y1 = GRID_BOX
    strip = Image.new("RGBA", (x1 - x0, (y1 - y0) * REEL_FRAMES), (0, 0, 0, 0))
    for k in range(REEL_FRAMES):
        strip.paste(sticker_image(PAGES[0], 1, k / REEL_FRAMES, 0.0).crop(GRID_BOX), (0, k * (y1 - y0)))
    return strip


def make_sun_strip(page, number):
    """LEVEL_FRAMES frames of the sun from dim to blazing (this page's title is in the picture)."""
    x0, y0, x1, y1 = SUN_BOX
    strip = Image.new("RGBA", (x1 - x0, (y1 - y0) * LEVEL_FRAMES), (0, 0, 0, 0))
    for k in range(LEVEL_FRAMES):
        strip.paste(sticker_image(page, number, 0.0, k / (LEVEL_FRAMES - 1)).crop(SUN_BOX), (0, k * (y1 - y0)))
    return strip


def sun_file(page):
    return "sun_%s.png" % page["name"].lower()


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
        end = bx + len([c for c in cells if c is not None or True]) * CELL_W - 10
        last_used = max(i for i, c in enumerate(cells) if c is not None)
        end = bx + (last_used + 1) * CELL_W - 10
        if end > hx + 10:
            rule = gradient(sc((end - hx, 2)), PINK, CYAN, horizontal=True)
            img.paste(rule, sc((hx, by + 17)))
        # cells
        for i, name in enumerate(cells):
            if name is None or name == "<":
                continue
            span = 2 if i + 1 < len(cells) and cells[i + 1] == "<" else 1
            cx = bx + i * CELL_W
            sy = by + SLOT_DY
            cap = CAPTION_OVERRIDE.get(name, CAPTIONS.get(name, name.upper()))
            draw_text(d, (cx + span * CELL_W / 2, sy + 4), cap, f_cap, (210, 250, 255), 1.5, "m")
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
        if left_used and right_used:
            xx = X0 + 4 * CELL_W
            d.line(sc((xx, ROW_Y[row] + 30, xx, ROW_Y[row] + ROW_H - 12)), fill=(96, 56, 170), width=sc(1))
    # divider between the rows
    d.line(sc((x0 + 18, ROW_Y[1] - 2, x1 - 18, ROW_Y[1] - 2)), fill=(72, 40, 140), width=sc(1))


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
def arc_colour(t):
    """Cyan -> purple -> pink along the knob's sweep."""
    return lerp_col(CYAN, PURPLE, t * 2) if t < 0.5 else lerp_col(PURPLE, PINK, (t - 0.5) * 2)


def knob_frame(t, bipolar):
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
            ld.arc(box, a, b + 0.6, fill=arc_colour((a - 135.0) / 270.0) + (255,), width=thick)
            a = b
        for ang in (a0, a1):       # round ends
            rr = r_arc - thick / 2
            ex, ey = c + rr * math.cos(math.radians(ang)), c + rr * math.sin(math.radians(ang))
            col = arc_colour((ang - 135.0) / 270.0) + (255,)
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
    d.line((x0, y0, x1, y1), fill=(235, 255, 255, 255), width=4 * s)
    for px, py in ((x0, y0), (x1, y1)):
        d.ellipse((px - 2 * s, py - 2 * s, px + 2 * s, py + 2 * s), fill=(235, 255, 255, 255))
    return img.resize((KNOB, KNOB), Image.LANCZOS)


def make_knob_strip(bipolar):
    strip = Image.new("RGBA", (KNOB, KNOB * KNOB_FRAMES), (0, 0, 0, 0))
    for i in range(KNOB_FRAMES):
        strip.paste(knob_frame(i / (KNOB_FRAMES - 1), bipolar), (0, i * KNOB))
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


def make_segment(size, caption, on):
    s = 4
    w, h = size
    img = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    body = (2 * s, 3 * s, (w - 2) * s, (h - 3) * s)
    r = 8 * s
    if on:
        fill = gradient((body[2] - body[0], body[3] - body[1]), PINK, PURPLE, horizontal=True).convert("RGBA")
        m = Image.new("L", fill.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, fill.size[0] - 1, fill.size[1] - 1), r, fill=255)
        glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(glow).rounded_rectangle(body, r, fill=(255, 46, 151, 140))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(2 * s)))
        img.paste(fill, (body[0], body[1]), m)
    d = ImageDraw.Draw(img)
    f = font(F_GOTHIC, 14, s)
    if on:
        d.rounded_rectangle(body, r, outline=(255, 210, 240, 255), width=s)
        col = (255, 255, 255, 255)
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


def knob_def(bipolar):
    vx, vy, vw, vh = VALUE_BOX
    children = [
        focus_entry(CELL_W),
        G.entry("knob", "Knob", G.knob_filmstrip_data("knob_bi.png" if bipolar else "knob_uni.png", KNOB_FRAMES),
                "%d %d %d %d" % (KNOB_POS[0], KNOB_POS[1], KNOB, KNOB), "No", "Show", "Always"),
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


def animation_entries(page, params):
    reel, level = params[REEL_PARAM].handle, params[LEVEL_PARAM].handle
    out = []
    for i, (cx, cy) in enumerate(REEL_CENTRES):
        out.append(film_entry("reel %d" % i, "reel_hub.png", REEL_FRAMES, reel, (cx - HUB // 2, cy - HUB // 2, HUB, HUB)))
    gx0, gy0, gx1, gy1 = GRID_BOX
    out.append(film_entry("grid", "grid.png", REEL_FRAMES, reel, (STICKER[0] + gx0, STICKER[1] + gy0, gx1 - gx0, gy1 - gy0)))
    sx0, sy0, sx1, sy1 = SUN_BOX
    out.append(film_entry("sun", sun_file(page), LEVEL_FRAMES, level, (STICKER[0] + sx0, STICKER[1] + sy0, sx1 - sx0, sy1 - sy0)))
    return out


def definition_name(p):
    if p.kind == "bool":
        return "vvToggle"
    if p.kind == "choice":
        return "vvSeg " + p.name
    return "vvKnobBi" if p.name in BIPOLAR else "vvKnobUni"


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
    referenced = {}
    tabs = []
    for ti, page in enumerate(PAGES):
        bg = G.obj(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", page["file"]))
        children = [G.entry("background", "Image", bg, "0 0 %d %d" % (W, H), "No", "Show", "Always")]
        children += animation_entries(page, params)
        for b, i, span, name in iter_controls(page):
            if name in CAPTION_SWAPS:
                pic = G.obj(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", swap_file(name)))
                children.append(G.entry("caption " + name, "Image", pic,
                                        "%d %d %d %d" % (cell_x(b, i), block_row(b) + SLOT_DY, CELL_W * span, CAPTION_H),
                                        "No", "Show", "Always",
                                        invalidating=["IndexedEnabling/%d/%d/%s" % (SWAP_STEP, SWAP_STEPS, params[SWAP_PARAM].handle)]))
        groups = []
        for b in range(4):
            if any(c is not None for c in page["blocks"][b][2]):
                groups.append("%d %d %d %d" % (cell_x(b, 0) - 4, block_row(b) + 2, 4 * CELL_W + 8, ROW_H - 4))
            else:
                groups.append("0 0 0 0")
        for b, i, span, name in iter_controls(page):
            p = params[name]
            referenced[p.index] = p
            dname = definition_name(p)
            if p.kind == "choice" and not any(k == dname for k, _ in defs):
                defs.append((dname, segment_def(name, span)))
            bounds = "%d %d %d %d" % (cell_x(b, i), block_row(b) + SLOT_DY, CELL_W * span, SLOT_H)
            children.append(G.entry(p.name, dname, G.obj(("version", 1), ("handleName", "Data")), bounds,
                                    "Yes", "Hide", "Always", remap=[("Data", p.handle)]))
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
def make_preview(page, bg, params, images):
    img = bg.convert("RGBA")
    # the animated parts, frozen at a lively moment
    for cx, cy in REEL_CENTRES:
        img.alpha_composite(images["reel_hub.png"].crop((0, 7 * HUB, HUB, 8 * HUB)), (cx - HUB // 2, cy - HUB // 2))
    gh = GRID_BOX[3] - GRID_BOX[1]
    img.alpha_composite(images["grid.png"].crop((0, 9 * gh, STICKER_W, 10 * gh)), (STICKER[0], STICKER[1] + GRID_BOX[1]))
    sh = SUN_BOX[3] - SUN_BOX[1]
    img.alpha_composite(images[sun_file(page)].crop((0, 12 * sh, SUN_BOX[2] - SUN_BOX[0], 13 * sh)),
                        (STICKER[0] + SUN_BOX[0], STICKER[1] + SUN_BOX[1]))
    d = ImageDraw.Draw(img)
    try:
        vf = ImageFont.truetype(os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\Windows\Fonts\Roboto-Bold.ttf"), 17)
    except OSError:
        vf = ImageFont.truetype(FONTS + "arialbd.ttf", 16)
    for b, i, span, name in iter_controls(page):
        p = params[name]
        x, y = cell_x(b, i), block_row(b) + SLOT_DY
        t = p.default if p.default is not None else 0.0
        if p.kind == "bool":
            img.alpha_composite(images["toggle_on.png" if t >= 0.5 else "toggle_off.png"], (x + TOGGLE_POS[0], y + TOGGLE_POS[1]))
        elif p.kind == "choice":
            size, offsets, _ = SEGMENTS[name]
            cur = int(round(t * (len(offsets) - 1)))
            for k, (dx, dy) in enumerate(offsets):
                img.alpha_composite(images[seg_file(name, k, k == cur)], (x + dx, y + dy))
        else:
            strip = images["knob_bi.png" if name in BIPOLAR else "knob_uni.png"]
            frame = int(round(t * (KNOB_FRAMES - 1)))
            img.alpha_composite(strip.crop((0, frame * KNOB, KNOB, (frame + 1) * KNOB)), (x + KNOB_POS[0], y + KNOB_POS[1]))
            vx, vy, vw, vh = VALUE_BOX
            text = p.display
            tw = d.textlength(text, font=vf)
            d.text((x + vx + (vw - tw) / 2, y + vy + 3), text, font=vf, fill=CYAN + (255,))
    return img.convert("RGB")


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
    for name in (REEL_PARAM, LEVEL_PARAM):
        if name not in params:
            sys.exit('make_skin: the plugin has no "%s" parameter (needed for the animation)' % name)
    missing = [p.name for p in plist if p.name not in shown and p.name not in (REEL_PARAM, LEVEL_PARAM)]
    if missing:
        print("make_skin: NOTE: parameters not on any page: " + ", ".join(missing))

    os.makedirs(SKINS, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    for f in os.listdir(SKINS):
        if f.lower().endswith((".png", ".json")):
            os.remove(os.path.join(SKINS, f))

    images = {}
    images["knob_uni.png"] = make_knob_strip(False)
    images["knob_bi.png"] = make_knob_strip(True)
    images["toggle_on.png"] = make_toggle(True)
    images["toggle_off.png"] = make_toggle(False)
    for name, (size, offsets, captions) in SEGMENTS.items():
        for i, cap in enumerate(captions):
            images[seg_file(name, i, True)] = make_segment(size, cap, True)
            images[seg_file(name, i, False)] = make_segment(size, cap, False)
    images["reel_hub.png"] = make_hub_strip()
    images["grid.png"] = make_grid_strip()
    for n, page in enumerate(PAGES, 1):
        images[sun_file(page)] = make_sun_strip(page, n)
    for fname, im in images.items():
        im.save(os.path.join(SKINS, fname), optimize=True)

    for n, page in enumerate(PAGES, 1):
        bg = make_background(page, n, params)
        bg.save(os.path.join(SKINS, page["file"]), optimize=True)
        # the other captions: the same background painted with them, cut out strip by strip
        swaps = [(b, i, span, name) for b, i, span, name in iter_controls(page) if name in CAPTION_SWAPS]
        if swaps:
            CAPTION_OVERRIDE.update(CAPTION_SWAPS)
            alt = make_background(page, n, params)
            CAPTION_OVERRIDE.clear()
            for b, i, span, name in swaps:
                x, y = cell_x(b, i), block_row(b) + SLOT_DY
                alt.crop((x, y, x + CELL_W * span, y + CAPTION_H)).save(os.path.join(SKINS, swap_file(name)), optimize=True)
        make_preview(page, bg, params, images).save(os.path.join(PREVIEW, "page_%d_%s.png" % (n, page["name"].lower())))

    with open(os.path.join(SKINS, "TUI.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(G.dump_json(build_tui(params)) + "\n")
    with open(os.path.join(SKINS, "Q-Links.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(G.dump_json(build_qlinks(params)) + "\n")

    total = sum(os.path.getsize(os.path.join(SKINS, f)) for f in os.listdir(SKINS))
    print("make_skin: wrote %d files to %s (%.0f KB); previews in %s" % (len(os.listdir(SKINS)), SKINS, total / 1024.0, PREVIEW))


if __name__ == "__main__":
    main()
