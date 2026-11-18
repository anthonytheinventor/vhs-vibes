#!/usr/bin/env python3
"""make_manual.py - builds the VHS Vibes user manual (PDF) from the text in manual/.

Run it from the plugin folder (Windows, macOS or Linux; needs Python 3.8+, Pillow for
make_skin.py's import, and Chrome, Edge or Chromium for the PDF):

    python tools/make_manual.py

Reads   plugin.json                 the version
        build/params.json           parameters, defaults, programs (written by tools/build.py)
        src/*.cpp                   the range and unit of every parameter (the addParam lines)
        tools/make_skin.py          pages, blocks, captions, geometry and palette (imported;
                                    nothing is drawn or written by it)
        skins/Q-Links.json          which Q-Link each control is on
        build/preview/page_N_*.png  the five screens (falls back to docs/)
        manual/manual.md            the chapters
        manual/controls.md          one entry per control
        manual/programs.md          one line per program
        manual/manual.css           the look
Writes  manual/VHS-Vibes-Manual.html   (kept: open it in a browser to check the text)
        manual/VHS-Vibes-Manual.pdf

At every release: edit the three .md files, run this, read the warnings.

The check that keeps the manual honest: the build STOPS if a parameter in build/params.json
has no entry in manual/controls.md, if a program has no line in manual/programs.md, or if a
page of the screen has no [[controls PAGE]] in manual/manual.md. It WARNS about entries for
parameters or programs that no longer exist, about a build/params.json that is older than the
source, and about screenshots that are older than build/params.json.

Options
    --html-only        write the HTML, skip the PDF
    --browser PATH     the Chrome / Edge / Chromium executable to print with
                       (default: looked up; or set VHS_MANUAL_BROWSER)
"""

import argparse
import glob
import html
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True          # importing make_skin must not leave __pycache__ in the repo

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
MANUAL = os.path.join(PROJECT, "manual")
OUT_HTML = os.path.join(MANUAL, "VHS-Vibes-Manual.html")
OUT_PDF = os.path.join(MANUAL, "VHS-Vibes-Manual.pdf")

WARNINGS = []


def warn(msg):
    WARNINGS.append(msg)
    print("make_manual: WARNING: " + msg)


def die(msgs):
    if isinstance(msgs, str):
        msgs = [msgs]
    print("make_manual: STOPPED. Nothing was written.", file=sys.stderr)
    for m in msgs:
        print("  - " + m, file=sys.stderr)
    sys.exit(1)


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def esc(s):
    return html.escape(str(s), quote=True)


def wide(s):
    """ASCII -> full-width characters, as on the cassette."""
    return "".join("　" if c == " " else chr(ord(c) + 0xFEE0) if "!" <= c <= "~" else c for c in s)


def rgb(c):
    return "#%02x%02x%02x" % tuple(c)


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


# --------------------------------------------------------------------------------------------
# Inputs
# --------------------------------------------------------------------------------------------
def load_skin_module():
    try:
        sys.path.insert(0, HERE)
        import make_skin
        return make_skin
    except Exception as e:  # noqa: BLE001  (any failure here has the same remedy)
        die("cannot import tools/make_skin.py for the page layout and palette (%s: %s). "
            "It needs Pillow and the SDK's tools/gen_ui.py." % (type(e).__name__, e))


def source_facts():
    """Ranges, units and choice lists from the addParam lines of src/*.cpp, by parameter name."""
    ranges, choices = {}, {}
    num = r"(-?\d+(?:\.\d+)?)f?"
    r_float = re.compile(r'(floatParam|logParam)\s*\(\s*"[^"]*"\s*,\s*"([^"]+)"\s*,\s*' + num + r"\s*,\s*" + num +
                         r"\s*,\s*" + num + r'\s*,\s*"([^"]*)"')
    r_choice = re.compile(r'choiceParam\s*\(\s*"[^"]*"\s*,\s*"([^"]+)"\s*,\s*\{([^}]*)\}')
    newest = 0.0
    for path in sorted(glob.glob(os.path.join(PROJECT, "src", "*.cpp")) + glob.glob(os.path.join(PROJECT, "src", "*.h"))):
        newest = max(newest, os.path.getmtime(path))
        text = read(path)
        for m in r_float.finditer(text):
            ranges[m.group(2)] = (float(m.group(3)), float(m.group(4)), m.group(6))
        for m in r_choice.finditer(text):
            choices[m.group(1)] = re.findall(r'"([^"]*)"', m.group(2))
    return ranges, choices, newest


def fmt_num(v):
    return ("%g" % v).replace("-", "\u2212")


def fmt_range(lo, hi, unit):
    if unit == "Hz":
        def hz(v):
            return ("%g kHz" % (v / 1000.0)) if v >= 1000 else ("%g Hz" % v)
        return "%s to %s" % (hz(lo), hz(hi))
    if lo < 0 < hi:
        return "%s to +%s%s" % (fmt_num(lo), fmt_num(hi), (" " + unit) if unit else "")
    return "%s to %s%s" % (fmt_num(lo), fmt_num(hi), (" " + unit) if unit else "")


def parse_entries(path, keyed):
    """'## name' entries. keyed: 'key: value' lines (continuation lines join); else free text."""
    text = re.sub(r"<!--.*?-->", "", read(path), flags=re.S)
    entries, name, key = {}, None, None
    for n, line in enumerate(text.split("\n"), 1):
        if line.startswith("## "):
            name = line[3:].strip()
            if name in entries:
                die("%s: entry '%s' appears twice" % (os.path.basename(path), name))
            entries[name] = {} if keyed else {"text": ""}
            key = None
            continue
        if not line.strip() or name is None:
            if line.strip():
                die("%s line %d: text before the first '## name'" % (os.path.basename(path), n))
            continue
        if keyed:
            m = re.match(r"^([a-z]+):\s*(.*)$", line)
            if m:
                key = m.group(1)
                if key not in ("what", "listen", "try", "note", "range"):
                    die("%s line %d: unknown key '%s:' in entry '%s' (use what, listen, try, note, range)"
                        % (os.path.basename(path), n, key, name))
                entries[name][key] = m.group(2).strip()
            elif key:
                entries[name][key] += ("\n- " + line.strip()[2:].strip()) if line.strip().startswith("- ") else (" " + line.strip())
            else:
                die("%s line %d: expected 'what: ...' in entry '%s'" % (os.path.basename(path), n, name))
        else:
            entries[name]["text"] = (entries[name]["text"] + " " + line.strip()).strip()
    return entries


def entry_parts(text):
    """A control entry's value: its opening sentence(s) and the list items that follow (lines that began "- ")."""
    parts = text.split("\n- ")
    return parts[0].strip(), [p.strip() for p in parts[1:]]


def entry_list(items, cls):
    return ('<ul class="%s">%s</ul>' % (cls, "".join("<li>%s</li>" % md_inline(i) for i in items))) if items else ""


class Data:
    pass


def load():
    d = Data()
    errors = []
    d.plugin = json.loads(read(os.path.join(PROJECT, "plugin.json")))
    d.version = d.plugin.get("version", "")
    d.vendor = d.plugin.get("manufacturer", "")
    d.url = d.plugin.get("url", "")

    pj = os.path.join(PROJECT, "build", "params.json")
    if not os.path.isfile(pj):
        die("build/params.json not found. It is written by a build (tools/build.py --target native is enough).")
    d.built = json.loads(read(pj))
    pj_time = os.path.getmtime(pj)

    # version of the build against plugin.json (1.5.0 <-> 1500)
    try:
        a, b, c = (int(x) for x in (d.version.split(".") + ["0", "0"])[:3])
        if int(d.built.get("version", -1)) != a * 1000 + b * 100 + c:
            warn("build/params.json was built as version %s but plugin.json says %s: rebuild before releasing the manual"
                 % (d.built.get("version"), d.version))
    except ValueError:
        pass

    d.mk = load_skin_module()
    mk = d.mk
    ranges, src_choices, src_time = source_facts()
    if src_time > pj_time + 2:
        warn("build/params.json is older than src/: defaults and the program list may be stale until the next build")

    # ---- parameters -------------------------------------------------------------------
    d.params = {}
    d.settings = []                         # everything a player can set, in parameter order
    for p in d.built["params"]:
        d.params[p["name"]] = p
        if p["name"] in (getattr(mk, "REEL_PARAM", "Reel"), getattr(mk, "LEVEL_PARAM", "Level")) or not p.get("automatable", True):
            continue                        # the two read-only meters
        d.settings.append(p)

    # ---- where each control is on the screen --------------------------------------------
    d.pages = []
    where = {}
    for pi, page in enumerate(mk.PAGES):
        blocks, title, jp, number = [], "", "", 0
        for b, (t, j, cells) in enumerate(page["blocks"]):
            if t:
                title, jp = t, j
            ctrls = []
            for i, name in enumerate(cells):
                if name is None or name == "<":
                    continue
                span = 2 if i + 1 < len(cells) and cells[i + 1] == "<" else 1
                number += 1
                c = {"name": name, "page": page["name"], "page_index": pi, "block": b, "cell": i, "span": span,
                     "block_title": title, "block_jp": jp, "number": number,
                     "caption": mk.CAPTIONS.get(name, name.upper()), "qlink": None}
                ctrls.append(c)
                if name in where:
                    errors.append("make_skin.py puts '%s' on two pages" % name)
                where[name] = c
            blocks.append({"index": b, "title": title, "jp": jp, "own_title": bool(t), "controls": ctrls})
        d.pages.append({"name": page["name"], "jp": page["jp"], "index": pi, "blocks": blocks})
    d.where = where

    for name in where:
        if name not in d.params:
            errors.append("make_skin.py shows '%s' on page %s but build/params.json has no such parameter "
                          "(stale build/params.json?)" % (name, where[name]["page"]))
    for p in d.settings:
        if p["name"] not in where:
            errors.append("parameter '%s' is on no page in tools/make_skin.py (PAGES): the manual cannot place it"
                          % p["name"])

    # ---- Q-Links as shipped ----------------------------------------------------------------
    d.program_qlinks = []
    ql = os.path.join(PROJECT, "skins", "Q-Links.json")
    if os.path.isfile(ql):
        q = json.loads(read(ql))
        by_index = {p["index"]: p["name"] for p in d.built["params"]}
        for entry in q.get("Screen Mode Q-Links", {}).get("map", []):
            for key, idx in entry.get("Q-Links", {}).items():
                if idx in by_index and by_index[idx] in where and where[by_index[idx]]["page_index"] == entry.get("Tab", 0) - 1:
                    where[by_index[idx]]["qlink"] = int(key.split()[-1])
        for key, idx in sorted(q.get("Program Mode Q-Links", {}).items(), key=lambda kv: int(kv[0].split()[-1])):
            if idx in by_index:
                d.program_qlinks.append((int(key.split()[-1]), by_index[idx]))
    else:
        warn("skins/Q-Links.json not found: the manual will not give Q-Link numbers")

    # ---- range, default, kind of each control ---------------------------------------------
    d.controls = parse_entries(os.path.join(MANUAL, "controls.md"), keyed=True)
    for p in d.settings:
        name = p["name"]
        c = where.get(name)
        if c is None:
            continue
        entry = d.controls.get(name)
        if entry is None:
            errors.append("manual/controls.md has no entry for the parameter '%s' (add '## %s' with a 'what:' line)"
                          % (name, name))
            continue
        if not entry.get("what"):
            errors.append("manual/controls.md: entry '%s' has no 'what:' line" % name)
        c["default"] = p["display"]
        if p["kind"] == "bool":
            c["kind"], c["range"] = "switch", "Off / On"
        elif p["kind"] == "choice":
            built = list(p["choices"])
            src = src_choices.get(name)
            if src and src != built:
                warn("'%s': src/ says the choices are %s, build/params.json still says %s. The manual uses the "
                     "source; rebuild so the two agree" % (name, " / ".join(src), " / ".join(built)))
                if len(src) == len(built) and p["display"] in built:
                    c["default"] = src[built.index(p["display"])]
            names = src or built
            c["kind"], c["range"] = "selector", " / ".join(names)
            for ch in names:
                if ch.lower() not in " ".join(entry.values()).lower():
                    warn("manual/controls.md: the entry '%s' never mentions its choice '%s'" % (name, ch))
        else:
            c["kind"] = "knob"
            if entry.get("range"):
                c["range"] = entry["range"]
            elif name in ranges:
                c["range"] = fmt_range(*ranges[name])
            else:
                errors.append("cannot find the range of '%s' in src/*.cpp (no floatParam/logParam line with that name). "
                              "Give it by hand with a 'range:' line in manual/controls.md" % name)
    for name in d.controls:
        if name not in d.params:
            warn("manual/controls.md has an entry '%s' but there is no such parameter any more: remove it" % name)
        elif name not in where:
            warn("manual/controls.md has an entry '%s' but that parameter is not a control on any page" % name)

    # ---- programs ---------------------------------------------------------------------------
    d.program_text = parse_entries(os.path.join(MANUAL, "programs.md"), keyed=False)
    d.programs = list(d.built.get("programs", []))
    for name in d.programs:
        if not d.program_text.get(name, {}).get("text"):
            errors.append("manual/programs.md has no line for the program '%s' (add '## %s' and one line)" % (name, name))
    for name in d.program_text:
        if name not in d.programs:
            warn("manual/programs.md has an entry '%s' but there is no such program any more: remove it" % name)

    # ---- screenshots ------------------------------------------------------------------------
    d.shots = {}
    for pg in d.pages:
        fname = "page_%d_%s.png" % (pg["index"] + 1, pg["name"].lower())
        for folder in (os.path.join(PROJECT, "build", "preview"), os.path.join(PROJECT, "docs")):
            path = os.path.join(folder, fname)
            if os.path.isfile(path):
                d.shots[pg["name"]] = path
                if os.path.getmtime(path) < pj_time - 120:
                    warn("%s is older than build/params.json: run tools/make_skin.py so the picture matches"
                         % os.path.relpath(path, PROJECT))
                break
        else:
            errors.append("no screenshot %s in build/preview/ or docs/ (tools/make_skin.py writes them)" % fname)

    if errors:
        die(errors)
    return d


# --------------------------------------------------------------------------------------------
# Drawing: the sunset, in SVG (after make_skin.sunset, but vector)
# --------------------------------------------------------------------------------------------
_uid = [0]


def uid(prefix):
    _uid[0] += 1
    return "%s%d" % (prefix, _uid[0])


def sunset_svg(d, w, h, horizon, sun_x, sun_r, seed=7, stars=26, grid_rows=7, sky_bottom=(150, 40, 150)):
    """w, h in mm; horizon and sun_x as mm. The striped sun over a neon grid."""
    mk = d.mk
    g_sky, g_sun, g_halo, m_sun = uid("sky"), uid("sun"), uid("halo"), uid("cut")
    out = ['<svg class="scene" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" preserveAspectRatio="none">' % (w, h)]
    out.append('<defs>'
               '<linearGradient id="%s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
               '<linearGradient id="%s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
               '<radialGradient id="%s"><stop offset="0.62" stop-color="%s" stop-opacity="0.75"/><stop offset="1" stop-color="%s" stop-opacity="0"/></radialGradient>'
               % (g_sky, rgb((22, 6, 58)), rgb(sky_bottom), g_sun, rgb(mk.YELLOW), rgb(mk.PINK), g_halo, rgb(mk.PINK_HI), rgb(mk.PINK_HI)))
    # the cuts through the sun, thicker towards the horizon
    out.append('<mask id="%s"><rect x="%g" y="%g" width="%g" height="%g" fill="#fff"/>' % (m_sun, sun_x - sun_r, horizon - sun_r - 1, 2 * sun_r, 2 * sun_r + 2))
    top = horizon - sun_r
    for k in range(5):
        yy = top + sun_r * (0.95 + 0.21 * k)
        out.append('<rect x="%g" y="%.3f" width="%g" height="%.3f" fill="#000"/>' % (sun_x - sun_r, yy, 2 * sun_r, sun_r * (0.037 + 0.024 * k)))
    out.append('</mask></defs>')
    out.append('<rect x="0" y="0" width="%g" height="%g" fill="url(#%s)"/>' % (w, horizon, g_sky))
    rnd = random.Random(seed)
    for _ in range(stars):
        sx, sy = rnd.uniform(1, w - 1), rnd.uniform(1, max(2.0, horizon - sun_r * 0.4))
        out.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="#fff" opacity="%.2f"/>' % (sx, sy, rnd.uniform(0.12, 0.3), rnd.uniform(0.5, 1.0)))
    out.append('<circle cx="%g" cy="%g" r="%g" fill="url(#%s)"/>' % (sun_x, horizon, sun_r * 1.45, g_halo))
    out.append('<circle cx="%g" cy="%g" r="%g" fill="url(#%s)" mask="url(#%s)"/>' % (sun_x, horizon, sun_r, g_sun, m_sun))
    out.append('<rect x="0" y="%g" width="%g" height="%g" fill="%s"/>' % (horizon, w, h - horizon, rgb((16, 4, 44))))
    floor = h - horizon
    for k in range(1, grid_rows + 1):
        yy = horizon + floor * (k / float(grid_rows)) ** 1.8
        out.append('<line x1="0" y1="%.2f" x2="%g" y2="%.2f" stroke="%s" stroke-width="0.28" opacity="0.9"/>' % (yy, w, yy, rgb(mk.CYAN)))
    a = w / 38.0
    for k in range(-40, 41):
        out.append('<line x1="%.2f" y1="%g" x2="%.2f" y2="%g" stroke="%s" stroke-width="0.26" opacity="0.9"/>'
                   % (sun_x + k * a, horizon, sun_x + k * a * (1.0 + floor / (a * 1.2)), h, rgb(mk.PINK)))
    out.append('<line x1="0" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="0.45"/>' % (horizon, w, horizon, rgb(mk.CYAN)))
    out.append('</svg>')
    return "".join(out)


def logo_svg(d):
    mk = d.mk
    g = uid("logo")
    return ('<svg class="logo" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 600 200">'
            '<defs><linearGradient id="%s" x1="0" y1="0" x2="0" y2="1"><stop offset="0.15" stop-color="%s"/><stop offset="0.9" stop-color="%s"/></linearGradient></defs>'
            '<text x="306" y="150" text-anchor="middle" font-family="%s" font-size="138" fill="%s">VHS Vibes</text>'
            '<text x="300" y="144" text-anchor="middle" font-family="%s" font-size="138" fill="url(#%s)">VHS Vibes</text></svg>'
            % (g, rgb((120, 30, 200)), rgb(mk.PINK), esc(FONT_LOGO_SVG), rgb(mk.CYAN), esc(FONT_LOGO_SVG), g))


def icon_svg(d, kind):
    mk = d.mk
    if kind == "knob":
        return ('<svg class="ico" viewBox="0 0 44 28"><path d="M14.93 22.07 A10 10 0 1 1 29.07 22.07" fill="none" stroke="%s" stroke-width="3" stroke-linecap="round"/>'
                '<path d="M14.93 22.07 A10 10 0 0 1 22 5" fill="none" stroke="%s" stroke-width="3" stroke-linecap="round"/>'
                '<circle cx="22" cy="15" r="5.8" fill="%s"/><line x1="22" y1="15" x2="22" y2="10.4" stroke="#fff" stroke-width="1.6" stroke-linecap="round"/></svg>'
                % (rgb((205, 185, 235)), rgb(mk.PINK), rgb(mk.INK)))
    if kind == "switch":
        return ('<svg class="ico" viewBox="0 0 44 28"><rect x="6" y="7" width="32" height="14" rx="7" fill="%s"/>'
                '<circle cx="31" cy="14" r="4.6" fill="#fff"/></svg>' % rgb(mk.PINK))
    return ('<svg class="ico" viewBox="0 0 44 28"><rect x="3" y="8" width="18" height="12" rx="3" fill="%s"/>'
            '<rect x="23" y="8" width="18" height="12" rx="3" fill="none" stroke="%s" stroke-width="1.4"/></svg>' % (rgb(mk.PINK), rgb(mk.PURPLE)))


# --------------------------------------------------------------------------------------------
# Screenshots with markers. All positions come from make_skin's geometry.
# --------------------------------------------------------------------------------------------
def marker_xy(mk, c):
    """Centre of the number marker of a control, in screenshot pixels.
    Knobs and switches: the free corner up and to the right of the knob (the arc ends 44 px from
    its centre, the caption above ends at slot y 19). Selectors fill their slot with buttons, so
    their marker goes to the left of the short caption."""
    x = mk.cell_x(c["block"], c["cell"])
    sy = mk.block_row(c["block"]) + mk.SLOT_DY
    if c["name"] in mk.SEGMENTS:
        half = 4.5 * len(c["caption"])                # caption: 15 px MS Gothic, half-width, 1.5 px tracking
        return x + c["span"] * mk.CELL_W / 2.0 - half - 17, sy + 11
    return x + 124, sy + 38


def shot_html(d, page, overlay, cls="shot"):
    rel = os.path.relpath(d.shots[page["name"]], MANUAL).replace(os.sep, "/")
    mk = d.mk
    return ('<div class="%s"><img src="%s" alt="The %s page"><svg viewBox="0 0 %d %d" xmlns="http://www.w3.org/2000/svg">%s</svg></div>'
            % (cls, esc(rel), esc(page["name"]), mk.W, mk.H, overlay))


def markers_svg(d, page):
    mk = d.mk
    out = []
    for blk in page["blocks"]:
        for c in blk["controls"]:
            x, y = marker_xy(mk, c)
            out.append('<circle cx="%.1f" cy="%.1f" r="11" fill="%s" stroke="%s" stroke-width="2"/>'
                       '<text x="%.1f" y="%.1f" text-anchor="middle" font-family="%s" font-weight="700" font-size="14" fill="%s">%d</text>'
                       % (x, y, rgb(mk.YELLOW), rgb(mk.NIGHT), x, y + 5, esc(FONT_BODY_SVG), rgb(mk.NIGHT), c["number"]))
    return "".join(out)


def group_rect(mk, b):
    """The Q-Link group rectangle of block b, as make_skin gives it to the MPC."""
    return mk.cell_x(b, 0) - 4, mk.block_row(b) + 2, 4 * mk.CELL_W + 8, mk.ROW_H - 4


def tag_svg(mk, x, y, letter):
    return ('<circle cx="%.1f" cy="%.1f" r="15" fill="%s" stroke="%s" stroke-width="2.5"/>'
            '<text x="%.1f" y="%.1f" text-anchor="middle" font-family="%s" font-weight="700" font-size="19" fill="%s">%s</text>'
            % (x, y, rgb(mk.YELLOW), rgb(mk.NIGHT), x, y + 6.5, esc(FONT_BODY_SVG), rgb(mk.NIGHT), letter))


def anatomy_html(d, lines):
    """::: anatomy block: 'region | text' lines. Regions are rectangles from make_skin."""
    mk = d.mk
    page = d.pages[0]
    first = page["blocks"][0]["controls"][0]
    cx, sy = mk.cell_x(first["block"], first["cell"]), mk.block_row(first["block"]) + mk.SLOT_DY
    vx, vy, vw, vh = mk.VALUE_BOX
    regions = {
        "label": (mk.LABEL[0], mk.LABEL[1], mk.LABEL[2] - mk.LABEL[0], mk.LABEL[3] - mk.LABEL[1]),
        "window": (mk.WINDOW[0], mk.WINDOW[1], mk.WINDOW[2] - mk.WINDOW[0], mk.WINDOW[3] - mk.WINDOW[1]),
        "sticker": (mk.STICKER[0], mk.STICKER[1], mk.STICKER[2] - mk.STICKER[0], mk.STICKER[3] - mk.STICKER[1]),
        "panel": (mk.PANEL[0], mk.PANEL[1], mk.PANEL[2] - mk.PANEL[0], mk.PANEL[3] - mk.PANEL[1]),
        "block": group_rect(mk, 1),
        "caption": (cx + 22, sy - 1, mk.CELL_W - 44, 22),
        "value": (cx + vx - 3, sy + vy - 3, vw + 6, vh + 6),
    }
    svg, keys = [], []
    for n, line in enumerate(l for l in lines if l.strip()):
        region, _, text = (s.strip() for s in line.partition("|"))
        if region not in regions:
            die("manual.md: ::: anatomy does not know the region '%s' (use %s)" % (region, ", ".join(regions)))
        letter = chr(ord("A") + n)
        x, y, w, h = regions[region]
        if region == "panel":       # drawn just outside the face label so it does not hide the block's frame
            x, y, w, h = x - 7, y - 7, w + 14, h + 14
        svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="10" fill="none" stroke="%s" stroke-width="4" stroke-dasharray="14 8"/>'
                   % (x, y, w, h, rgb(mk.YELLOW)))
        tx, ty = (x + w, y + h) if region in ("value", "caption") else (x + w - 6, y + 10) if region == "block" else (x + 10, y + h - 8) if region == "panel" else (x + 6, y + 8)
        if region in ("value", "caption"):
            tx, ty = x + w + 14, y + h / 2.0
        svg.append(tag_svg(mk, tx, ty, letter))
        keys.append('<div><b class="k">%s</b><span>%s</span></div>' % (letter, md_inline(text)))
    return '<figure>%s<div class="keys">%s</div></figure>' % (shot_html(d, page, "".join(svg)), "".join(keys))


def qrange(ctrls_or_block, mk=None):
    qs = [c["qlink"] - c["cell"] for c in ctrls_or_block if c.get("qlink")]
    if not qs:
        return ""
    return "Q-LINK %d-%d" % (qs[0], qs[0] + 3)      # a plain hyphen: MS Gothic has no en dash


def qlink_map_html(d):
    mk = d.mk
    page = d.pages[0]
    svg = []
    for blk in page["blocks"]:
        if not blk["controls"]:
            continue
        x, y, w, h = group_rect(mk, blk["index"])
        label = qrange(blk["controls"]) or "ONE GROUP"
        svg.append('<rect x="%d" y="%d" width="%d" height="%d" rx="12" fill="%s" fill-opacity="0.10" stroke="%s" stroke-width="4"/>'
                   % (x, y, w, h, rgb(mk.YELLOW), rgb(mk.YELLOW)))
        pw = 190
        svg.append('<rect x="%.1f" y="%.1f" width="%d" height="34" rx="17" fill="%s" stroke="%s" stroke-width="2"/>'
                   '<text x="%.1f" y="%.1f" text-anchor="middle" font-family="%s" font-weight="700" font-size="18" fill="%s">%s</text>'
                   % (x + w / 2.0 - pw / 2.0, y + 88, pw, rgb(mk.NIGHT), rgb(mk.YELLOW), x + w / 2.0, y + 111.5,
                      esc(FONT_BODY_SVG), rgb(mk.YELLOW), esc(label)))
    cap = "The four Q-Link groups of a page, here on %s. The knobs of a group run left to right." % page["name"]
    return '<figure>%s<figcaption>%s</figcaption></figure>' % (shot_html(d, page, "".join(svg)), esc(cap))


def program_qlinks_html(d):
    if not d.program_qlinks:
        return "<p>(skins/Q-Links.json was not found when this manual was built.)</p>"
    rows = []
    for q, name in d.program_qlinks:
        c = d.where.get(name)
        cap = c["caption"] if c else name.upper()
        place = "%s \u203a %s" % (c["page"], c["block_title"]) if c else ""
        rows.append('<tr><td class="no">Q-LINK %d</td><td class="cap">%s</td><td>%s</td></tr>' % (q, esc(cap), esc(place)))
    half = (len(rows) + 1) // 2
    head = '<thead><tr><th>Q-LINK</th><th>CONTROL</th><th>PAGE, BLOCK</th></tr></thead>'
    return ('<div class="cols2"><table class="ql">%s<tbody>%s</tbody></table><table class="ql">%s<tbody>%s</tbody></table></div>'
            % (head, "".join(rows[:half]), head, "".join(rows[half:])))


# --------------------------------------------------------------------------------------------
# The small Markdown
# --------------------------------------------------------------------------------------------
def md_inline(s):
    s = html.escape(s, quote=False)
    s = re.sub(r"`([^`]+)`", r"<code>\1</code>", s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", s)
    s = re.sub(r"\^([^^\n]+)\^", r'<span class="pr">\1</span>', s)      # ^NAME^: a name as printed on the screen
    # full-width and Japanese runs get the plugin's caption font wherever they appear
    s = re.sub("([　-ヿ一-鿿＀-￯]+)", r'<span class="fw">\1</span>', s)
    return s


def keyvals(lines, where):
    out = {}
    for line in lines:
        if not line.strip():
            continue
        m = re.match(r"^([a-z]+):\s*(.*)$", line)
        if not m:
            die("manual.md: expected 'key: value' in the ::: %s block, got: %s" % (where, line))
        out[m.group(1)] = m.group(2).strip()
    return out


class Book:
    def __init__(self, d):
        self.d = d
        self.chapters = []          # dicts: id, title, jp, word, toc, epigraph, kind, page
        self.used_pages = set()
        self.has_programs = False
        self.toc_pages = {}

    # ---- directives ----------------------------------------------------------------------
    def directive(self, text):
        d = self.d
        parts = text.split()
        name, args = parts[0], parts[1:]
        if name == "toc":
            return "<!--TOC-->"
        if name == "pagebreak":
            return '<div style="break-before: page"></div>'
        if name == "controls":
            if not args or args[0] not in [p["name"] for p in d.pages]:
                die("manual.md: [[controls %s]] names no page of the plugin (pages: %s)"
                    % (" ".join(args), ", ".join(p["name"] for p in d.pages)))
            self.used_pages.add(args[0])
            return self.controls_html([p for p in d.pages if p["name"] == args[0]][0])
        if name == "programs":
            self.has_programs = True
            rows = ['<tr><td class="no">%02d</td><td class="name">%s</td><td>%s</td></tr>'
                    % (n, esc(p), md_inline(d.program_text[p]["text"])) for n, p in enumerate(d.programs, 1)]
            return ('<table class="programs long"><thead><tr><th>NO.</th><th>PROGRAM</th><th>WHAT IS ON IT</th></tr></thead><tbody>%s</tbody></table>'
                    % "".join(rows))
        if name == "switches":
            rows = []
            for pg in d.pages:
                for blk in pg["blocks"]:
                    for c in blk["controls"]:
                        if c["kind"] == "switch":
                            rows.append('<tr><td class="cap">%s</td><td class="place">%s \u203a %s</td><td>%s</td><td>%s</td></tr>'
                                        % (esc(c["caption"]), esc(c["page"]), esc(c["block_title"]), esc(c["default"]),
                                           md_inline(d.controls[c["name"]]["what"].replace("\n- ", " "))))
            return ('<table class="switches whole"><thead><tr><th>SWITCH</th><th>PAGE, BLOCK</th><th>DEFAULT</th><th>WHAT IT DOES</th></tr></thead>'
                    '<tbody>%s</tbody></table>' % "".join(rows))
        if name == "screenshot":
            pname, _, caption = " ".join(args).partition("|")
            page = [p for p in d.pages if p["name"] == pname.strip()]
            if not page:
                die("manual.md: [[screenshot %s]] names no page of the plugin" % pname.strip())
            cap = "<figcaption>%s</figcaption>" % md_inline(caption.strip()) if caption.strip() else ""
            return '<figure class="hero">%s%s</figure>' % (shot_html(d, page[0], ""), cap)
        if name == "pages":
            rows = []
            for pg in d.pages:
                titles = []
                for blk in pg["blocks"]:
                    if blk["controls"] and blk["title"] not in titles:
                        titles.append(blk["title"])
                rows.append('<tr><td class="cap">%s\u3000%s</td><td>%s</td><td>%d</td></tr>'
                            % (esc(wide(pg["name"])), esc(pg["jp"]), esc(", ".join(titles)), sum(len(b["controls"]) for b in pg["blocks"])))
            return ('<table class="pages whole"><thead><tr><th>PAGE</th><th>BLOCKS ON IT</th><th>CONTROLS</th></tr></thead><tbody>%s</tbody></table>'
                    % "".join(rows))
        if name == "qlink-map":
            return qlink_map_html(d)
        if name == "program-qlinks":
            return program_qlinks_html(d)
        die("manual.md: unknown directive [[%s]]" % text)

    def controls_html(self, page):
        d = self.d
        out = []
        for blk in page["blocks"]:
            if not blk["controls"]:
                continue
            q = qrange(blk["controls"])
            out.append('<div class="blk"><span class="t">%s</span><span class="jp">%s</span><span class="r"></span><span class="q">%s</span></div>'
                       % (esc(blk["title"]), esc(blk["jp"]), esc(q)))
            for c in blk["controls"]:
                e = d.controls[c["name"]]
                more = ""
                for key, tag in (("listen", "LISTEN FOR"), ("try", "START AT"), ("note", "NOTE")):
                    if e.get(key):
                        head, items = entry_parts(e[key])
                        more += '<p class="more"><span class="tag">%s</span>%s</p>%s' % (tag, md_inline(head), entry_list(items, "more"))
                place = "%s \u203a %s" % (c["page"], c["block_title"])
                if c.get("qlink"):
                    place += " \u00b7 Q-Link %d" % c["qlink"]
                rows = [("WHERE", place, ""), ("RANGE", c["range"], ""), ("DEFAULT", c["default"], "v"), ("PARAMETER", c["name"], "")]
                dl = "".join('<dt>%s</dt><dd class="%s">%s</dd>' % (k, cls, esc(v)) for k, v, cls in rows)
                out.append('<div class="ctl" id="ctl-%s"><div class="meta"><div class="id"><span class="n">%d</span><span class="cap">%s</span>%s</div>'
                           '<dl>%s</dl></div><div class="txt"><p class="what">%s</p>%s%s</div></div>'
                           % (slug(c["name"]), c["number"], esc(c["caption"]), icon_svg(d, c["kind"]), dl,
                              md_inline(entry_parts(e["what"])[0]), entry_list(entry_parts(e["what"])[1], "what"), more))
        return "".join(out)

    # ---- blocks -----------------------------------------------------------------------------
    def blocks(self, lines):
        out, para, i = [], [], 0

        def flush():
            if para:
                out.append("<p>%s</p>" % md_inline(" ".join(s.strip() for s in para)))
                del para[:]

        while i < len(lines):
            line = lines[i]
            s = line.strip()
            if not s:
                flush()
                i += 1
            elif s.startswith(":::"):
                flush()
                head = s[3:].strip()
                j = i + 1
                while j < len(lines) and lines[j].strip() != ":::":
                    j += 1
                if j >= len(lines):
                    die("manual.md: a '::: %s' block is never closed with ':::'" % head)
                body = lines[i + 1:j]
                kind, _, arg = head.partition(" ")
                if kind == "note":
                    out.append('<div class="note"><div class="nt">%s</div>%s</div>' % (md_inline(arg), self.blocks(body)))
                elif kind == "aside":
                    out.append('<div class="aside">%s</div>' % md_inline(" ".join(b.strip() for b in body if b.strip())))
                elif kind == "signal":
                    out.append(signal_html(self.d, body))
                elif kind == "anatomy":
                    out.append(anatomy_html(self.d, body))
                else:
                    die("manual.md: unknown block ':::%s'" % head)
                i = j + 1
            elif re.match(r"^\[\[.+\]\]$", s):
                flush()
                out.append(self.directive(s[2:-2].strip()))
                i += 1
            elif s.startswith("### "):
                flush()
                out.append("<h3>%s</h3>" % md_inline(s[4:]))
                i += 1
            elif s.startswith("## "):
                flush()
                out.append("<h2><span>%s</span></h2>" % md_inline(s[3:]))
                i += 1
            elif s.startswith("|"):
                flush()
                rows = []
                while i < len(lines) and lines[i].strip().startswith("|"):
                    rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                    i += 1
                if len(rows) < 2 or not all(re.match(r"^:?-+:?$", c) for c in rows[1]):
                    die("manual.md: a table needs a header row and a |---| row (near: %s)" % " | ".join(rows[0]))
                head = "".join("<th>%s</th>" % md_inline(c.upper()) for c in rows[0])
                body = "".join("<tr>%s</tr>" % "".join("<td>%s</td>" % md_inline(c) for c in r) for r in rows[2:])
                out.append('<table class="%s"><thead><tr>%s</tr></thead><tbody>%s</tbody></table>'
                           % ("long" if len(rows) > 14 else "whole", head, body))
            elif re.match(r"^(- |\d+\. )", s):
                flush()
                ordered = not s.startswith("- ")
                items = []
                while i < len(lines):
                    t = lines[i].strip()
                    m = re.match(r"^(- |\d+\. )(.*)$", t)
                    if m and (m.group(1) == "- ") != ordered:
                        items.append(m.group(2))
                    elif t and items and lines[i].startswith("  "):
                        items[-1] += " " + t
                    else:
                        break
                    i += 1
                tag = "ol" if ordered else "ul"
                out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % md_inline(x) for x in items), tag))
            else:
                para.append(line)
                i += 1
        flush()
        return "".join(out)

    # ---- chapters ---------------------------------------------------------------------------
    def banner(self, ch, n, total):
        d = self.d
        scene = sunset_svg(d, 178, 43, 28.5, 138, 15.5, seed=11 + n, stars=22, grid_rows=5)
        return ('<div class="banner">%s<div class="num">%02d\uff0f%02d</div><div class="word">%s</div>'
                '<div class="full%s">%s\u3000%s</div><div class="epi">%s</div></div>'
                % (scene, n, total, esc(ch["word"]), " tight" if len(ch["title"]) + len(ch["jp"]) > 16 else "",
                   esc(ch["title"]), esc(ch["jp"]), md_inline(ch.get("epigraph", ""))))

    def opener(self, ch):
        d = self.d
        mk = d.mk
        page = ch["page"]
        scene = sunset_svg(d, 210, 192, 100, 152, 36, seed=3 + page["index"], stars=46, grid_rows=9)
        cells = []
        for blk in page["blocks"]:
            if not blk["controls"]:
                cells.append('<div class="lg-block empty"></div>')
                continue
            inner, col = [], 0
            for c in blk["controls"]:
                while col < c["cell"]:
                    inner.append('<div class="lg-cell"></div>')
                    col += 1
                inner.append('<div class="lg-cell%s"><span class="n">%d</span><span class="c">%s</span><span class="d">%s</span></div>'
                             % (" x2" if c["span"] == 2 else "", c["number"], esc(c["caption"]), esc(c["default"])))
                col += c["span"]
            head = ('<div class="lg-head"><span class="q">%s</span>%s<span class="jp">%s</span></div>'
                    % (esc(qrange(blk["controls"])), esc(blk["title"]), esc(blk["jp"])))
            cells.append('<div class="lg-block">%s<div class="lg-cells">%s</div></div>' % (head, "".join(inner)))
        strip = "ＶＨＳ　ＶＩＢＥＳ　／　ＨＩ－ＦＩ　ＳＴＥＲＥＯ　／　ビデオカセット　／　" + wide("v" + d.version)
        return ('<div class="opener" id="%s">%s<div class="fade"></div><div class="num">%02d\uff0f%02d</div><div class="word">%s</div>'
                '<div class="full">%s\u3000%s</div><div class="epi">%s</div>%s'
                '<div class="legend"><div class="lt">ＷＨＥＲＥ　ＴＨＩＮＧＳ　ＡＲＥ</div>'
                '<p class="hint">The numbers on the picture are the numbers on the pages that follow. Under each name: its value in a new instance.</p>'
                '<div class="lg-grid">%s</div></div><div class="strip">%s</div></div>'
                % (ch["id"], scene, page["index"] + 1, len(d.pages), esc(page["name"]), esc(wide(page["name"])), esc(page["jp"]),
                   md_inline(ch.get("epigraph", "")), shot_html(d, page, markers_svg(d, page)), "".join(cells), esc(strip)))

    def cover(self, kv):
        d = self.d
        scene = sunset_svg(d, 210, 297, 206, 105, 50, seed=5, stars=90, grid_rows=10, sky_bottom=(190, 44, 150))
        strip = "ＶＨＳ　ＶＩＢＥＳ　／　ＨＩ－ＦＩ　ＳＴＥＲＥＯ　／　ビデオカセット　／　" + wide("v" + d.version)
        return ('<div class="cover">%s<div class="scan"></div><div class="kana">%s</div>'
                '<div class="label"><div class="l1">ＶＨＳ　バイブス</div><div class="l2">Ｔ－１２０</div>%s<div class="stripes"></div>'
                '<div class="l3">ａｅｓｔｈｅｔｉｃ　ｔａｐｅ</div><div class="l4">%s</div></div>'
                '<div class="manual">%s</div><div class="line">%s</div><div class="epi">%s</div><div class="strip">%s</div></div>'
                % (scene, esc(kv.get("kana", "")), logo_svg(d), esc(d.vendor), esc(kv.get("manual", "")), md_inline(kv.get("line", "")),
                   md_inline(kv.get("epigraph", "")), esc(strip)))

    def toc_html(self):
        rows = []
        for ch in self.chapters:
            if ch.get("toc") == "-":
                continue
            rows.append('<a href="#%s"><span class="t">%s<span class="jp">\u3000%s</span></span><span class="w">%s</span>'
                        '<span class="dots"></span><span class="p">%s</span></a>'
                        % (ch["id"], esc(ch["title"]), esc(ch["jp"]), md_inline(ch.get("toc", "")), self.toc_pages.get(ch["id"], "")))
        return '<div class="toc">%s</div>' % "".join(rows)

    def render(self, text):
        d = self.d
        # split into cover / chapter / page markers and the text between them
        lines = text.split("\n")
        parts, buf, i = [], [], 0
        while i < len(lines):
            m = re.match(r"^:::\s*(cover|chapter|page)\b\s*(.*)$", lines[i].strip())
            if m:
                j = i + 1
                while j < len(lines) and lines[j].strip() != ":::":
                    j += 1
                if j >= len(lines):
                    die("manual.md: the '::: %s' block is never closed with ':::'" % m.group(1))
                parts.append(("md", buf))
                buf = []
                parts.append((m.group(1), m.group(2).strip(), keyvals(lines[i + 1:j], m.group(1))))
                i = j + 1
            else:
                buf.append(lines[i])
                i += 1
        parts.append(("md", buf))

        for part in parts:
            if part[0] == "chapter":
                kv = part[2]
                for need in ("id", "title", "jp", "word"):
                    if need not in kv:
                        die("manual.md: a ::: chapter block has no '%s:' line" % need)
                kv["kind"] = "chapter"
                self.chapters.append(kv)
            elif part[0] == "page":
                page = [p for p in d.pages if p["name"] == part[1]]
                if not page:
                    die("manual.md: '::: page %s' names no page of the plugin (pages: %s)" % (part[1], ", ".join(p["name"] for p in d.pages)))
                kv = part[2]
                kv.update(kind="page", page=page[0], id="page-" + page[0]["name"].lower(), title=wide(page[0]["name"]),
                          jp=page[0]["jp"], word=page[0]["name"])
                self.chapters.append(kv)
        ids = [c["id"] for c in self.chapters]
        if len(set(ids)) != len(ids):
            die("manual.md: two chapters have the same id")

        out, n, open_section = [], 0, False
        banners = [c for c in self.chapters if c["kind"] == "chapter"]
        for part in parts:
            if part[0] == "md":
                out.append(self.blocks(part[1]))
                continue
            if open_section:
                out.append("</section>")
                open_section = False
            if part[0] == "cover":
                out.append(self.cover(part[2]))
                continue
            ch = part[2]
            if ch["kind"] == "page":
                out.append(self.opener(ch))
                out.append('<section class="after-opener">')
            else:
                n += 1
                out.append('<section class="chapter" id="%s">%s' % (ch["id"], self.banner(ch, n, len(banners))))
            open_section = True
        if open_section:
            out.append("</section>")

        missing = [p["name"] for p in d.pages if p["name"] not in self.used_pages]
        errors = ["manual.md has no [[controls %s]]: the controls of that page would be missing from the manual" % m for m in missing]
        if not self.has_programs:
            errors.append("manual.md has no [[programs]]: the program list would be missing from the manual")
        if errors:
            die(errors)
        return "".join(out)


# --------------------------------------------------------------------------------------------
# The signal path: a tape threading through the stages
# --------------------------------------------------------------------------------------------
PAGE_COLOURS = None     # filled in main(): page name -> colour on paper


def signal_html(d, lines):
    mk = d.mk
    stages = []
    for line in lines:
        if not line.strip():
            continue
        f = [s.strip() for s in line.split("|")] + ["", "", ""]
        if f[1] not in PAGE_COLOURS:
            die("manual.md: ::: signal stage '%s' names the page '%s', which the plugin does not have" % (f[0], f[1]))
        stages.append({"name": f[0], "page": f[1], "sub": f[2], "flags": f[3].split()})
    cols, W, mx, gap, bh, vgap, top = 5, 712.0, 58.0, 20.0, 46.0, 28.0, 8.0
    bw = (W - 2 * mx - (cols - 1) * gap) / cols
    rows = (len(stages) + cols - 1) // cols
    H = top + rows * bh + (rows - 1) * vgap + 8
    pos = []
    for i in range(len(stages)):
        r, c = divmod(i, cols)
        if r % 2:
            c = cols - 1 - c
        pos.append((mx + c * (bw + gap), top + r * (bh + vgap), r))
    ink, pink = rgb(mk.INK), rgb(mk.PINK)
    R = (bh + vgap) / 2.0
    path = ["M 30 %.1f" % (pos[0][1] + bh / 2)]
    arrows = []

    def arrow(x, y, direction):
        s = 5.5
        if direction == "r":
            pts = [(x - s, y - s), (x + s, y), (x - s, y + s)]
        elif direction == "l":
            pts = [(x + s, y - s), (x - s, y), (x + s, y + s)]
        else:
            pts = [(x - s, y - s), (x, y + s), (x + s, y - s)]
        arrows.append('<polygon points="%s" fill="%s"/>' % (" ".join("%.1f,%.1f" % p for p in pts), pink))

    arrow(mx - 12, pos[0][1] + bh / 2, "r")
    for i, (x, y, r) in enumerate(pos):
        cy = y + bh / 2
        path.append("L %.1f %.1f" % (x + bw / 2, cy))
        if i + 1 == len(pos):
            going_right = r % 2 == 0
            xe = x + bw + 34 if going_right else x - 34
            path.append("L %.1f %.1f" % (xe, cy))
            arrow(x + bw + 12 if going_right else x - 12, cy, "r" if going_right else "l")
            end = (xe + (6 if going_right else -6), cy, "start" if going_right else "end")
        elif pos[i + 1][2] != r:
            going_right = r % 2 == 0
            xe = (W - mx + 10) if going_right else (mx - 10)
            path.append("L %.1f %.1f A %.1f %.1f 0 0 %d %.1f %.1f" % (xe, cy, R, R, 1 if going_right else 0, xe, cy + 2 * R))
            arrow(xe + (R if going_right else -R), cy + R, "d")
        else:
            nx = pos[i + 1][0]
            arrow((x + bw + nx) / 2 if nx > x else (nx + bw + x) / 2, cy, "r" if nx > x else "l")
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g">' % (W, H)]
    svg.append('<path d="%s" fill="none" stroke="%s" stroke-width="5" stroke-linejoin="round" stroke-linecap="round" opacity="0.9"/>' % (" ".join(path), ink))
    svg.append('<path d="%s" fill="none" stroke="#fff" stroke-width="0.8" stroke-dasharray="1 5" opacity="0.5"/>' % " ".join(path))
    svg.append('<text x="4" y="%.1f" font-family="%s" font-size="10" fill="%s">ＩＮ</text>' % (pos[0][1] + bh / 2 + 3.5, esc(FONT_GOTHIC_SVG), ink))
    svg.append('<text x="%.1f" y="%.1f" text-anchor="%s" font-family="%s" font-size="10" fill="%s">ＯＵＴ</text>'
               % (end[0], end[1] + 3.5, end[2], esc(FONT_GOTHIC_SVG), ink))
    for st, (x, y, r) in zip(stages, pos):
        col = PAGE_COLOURS[st["page"]]
        dash = ' stroke-dasharray="5 3"' if "optional" in st["flags"] else ""
        svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="7" fill="#fff" stroke="%s" stroke-width="2"%s/>' % (x, y, bw, bh, col, dash))
        svg.append('<text x="%.1f" y="%.1f" text-anchor="middle" font-family="%s" font-size="6.6" letter-spacing="0.8" fill="%s">%s</text>'
                   % (x + bw / 2, y + 11.5, esc(FONT_GOTHIC_SVG), col, esc(st["page"])))
        svg.append('<text x="%.1f" y="%.1f" text-anchor="middle" font-family="%s" font-weight="700" font-size="11.5" fill="%s">%s</text>'
                   % (x + bw / 2, y + 26, esc(FONT_BODY_SVG), ink, esc(st["name"])))
        svg.append('<text x="%.1f" y="%.1f" text-anchor="middle" font-family="%s" font-size="7.4" fill="#5a3c85">%s</text>'
                   % (x + bw / 2, y + 37.5, esc(FONT_BODY_SVG), esc(st["sub"])))
        if "hq" in st["flags"]:
            svg.append('<rect x="%.1f" y="%.1f" width="22" height="11" rx="5.5" fill="%s"/>'
                       '<text x="%.1f" y="%.1f" text-anchor="middle" font-family="%s" font-weight="700" font-size="7" fill="%s">HQ</text>'
                       % (x + bw - 27, y - 5.5, ink, x + bw - 16, y + 2.6, esc(FONT_BODY_SVG), rgb(mk.YELLOW)))
    svg += arrows
    svg.append("</svg>")
    key = "".join('<span><i style="background:%s"></i>%s</span>' % (PAGE_COLOURS[p["name"]], esc(wide(p["name"]))) for p in d.pages)
    key += ('<span class="plain"><i style="background:%s"></i>HQ: runs at twice the sample rate when HQ is on</span>'
            '<span class="plain"><i style="border:.4mm dashed %s"></i>Noise enters at one of these two places (Position)</span>' % (ink, rgb(mk.ORANGE)))
    return '<div class="signal">%s</div><div class="sig-key">%s</div>' % ("".join(svg), key)


# --------------------------------------------------------------------------------------------
# Fonts: the plugin's own (see make_skin.py), found on this machine, embedded by the browser
# --------------------------------------------------------------------------------------------
FONT_LOGO_SVG = FONT_BODY_SVG = FONT_GOTHIC_SVG = ""


def font_stacks():
    """[(role, css stack, [(family, file names to look for)])]. The first family whose file is
    installed is reported; the stack is given to the browser whole, so a missing font falls back."""
    roles = [
        ("gothic", "captions, full-width titles, Japanese", [("MS Gothic", ["msgothic.ttc"]), ("Yu Gothic", ["YuGothR.ttc", "YuGothM.ttc"]),
                                                              ("Meiryo", ["meiryo.ttc"]), ("Noto Sans CJK JP", []), ("Hiragino Sans", [])], "monospace"),
        ("head", "headings", [("Times New Roman", ["timesbi.ttf"]), ("Liberation Serif", []), ("Times", [])], "serif"),
        ("logo", "the logo", [("Brush Script MT", ["BRUSHSCI.TTF"]), ("Segoe Script", ["segoesc.ttf"]), ("Snell Roundhand", [])], "cursive"),
        ("body", "running text", [("Roboto", ["Roboto-Regular.ttf"]), ("Segoe UI", ["segoeui.ttf"]), ("Helvetica Neue", []), ("Arial", ["arial.ttf"])], "sans-serif"),
        ("mono", "file names", [("Consolas", ["consola.ttf"]), ("Menlo", []), ("DejaVu Sans Mono", [])], "monospace"),
    ]
    dirs = [os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts"),
            os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "Windows", "Fonts"),
            "/usr/share/fonts", "/Library/Fonts", os.path.expanduser("~/Library/Fonts")]
    stacks, report = {}, []
    for role, use, families, generic in roles:
        stacks[role] = ", ".join('"%s"' % f for f, _ in families) + ", " + generic
        found = None
        for fam, files in families:
            if any(os.path.isfile(os.path.join(dd, fn)) for dd in dirs if dd for fn in files):
                found = fam
                break
        if found != families[0][0]:
            warn("font for %s: '%s' not found, the browser will fall back (%s)" % (use, families[0][0], found or "its own default"))
        report.append("%s: %s" % (use, found or "(browser fallback)"))
    return stacks, report


# --------------------------------------------------------------------------------------------
# PDF
# --------------------------------------------------------------------------------------------
def find_browser(explicit):
    cands = []
    if explicit:
        cands.append(explicit)
    if os.environ.get("VHS_MANUAL_BROWSER"):
        cands.append(os.environ["VHS_MANUAL_BROWSER"])
    if sys.platform == "win32":
        try:
            import winreg
            for exe in ("chrome.exe", "msedge.exe"):
                for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
                    try:
                        with winreg.OpenKey(hive, r"SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\\" + exe) as k:
                            cands.append(winreg.QueryValueEx(k, None)[0])
                    except OSError:
                        pass
        except ImportError:
            pass
        for base in (os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)"), os.environ.get("LOCALAPPDATA")):
            if base:
                cands.append(os.path.join(base, "Google", "Chrome", "Application", "chrome.exe"))
                cands.append(os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe"))
    elif sys.platform == "darwin":
        cands += ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                  "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
                  "/Applications/Chromium.app/Contents/MacOS/Chromium"]
    for name in ("chrome", "google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "msedge", "microsoft-edge"):
        w = shutil.which(name)
        if w:
            cands.append(w)
    for c in cands:
        if c and os.path.isfile(c.strip('"')):
            return c.strip('"')
    die("no Chrome, Edge or Chromium found to print the PDF. Install one, or name it with --browser PATH "
        "(or the environment variable VHS_MANUAL_BROWSER). The HTML can be built alone with --html-only.")


def print_pdf(browser, html_path, pdf_path):
    profile = tempfile.mkdtemp(prefix="vhs-manual-browser-")
    tmp_pdf = pdf_path + ".tmp"
    url = "file:///" + html_path.replace(os.sep, "/").lstrip("/").replace(" ", "%20")
    try:
        last = ""
        for headless in ("--headless=new", "--headless"):
            if os.path.exists(tmp_pdf):
                os.remove(tmp_pdf)
            cmd = [browser, headless, "--disable-gpu", "--no-first-run", "--disable-extensions", "--no-pdf-header-footer",
                   "--user-data-dir=" + profile, "--print-to-pdf=" + tmp_pdf, url]
            try:
                r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
                last = r.stdout.decode("utf-8", "replace")[-600:]
            except subprocess.TimeoutExpired:
                last = "timed out after 300 s"
            if os.path.isfile(tmp_pdf) and os.path.getsize(tmp_pdf) > 1000:
                with open(tmp_pdf, "rb") as f:
                    data = f.read()
                if data[:5] == b"%PDF-":
                    try:
                        os.replace(tmp_pdf, pdf_path)
                    except OSError as e:
                        die("cannot write %s (%s). Is the PDF open in a viewer? Close it and run again." % (pdf_path, e))
                    return data
        die("the browser did not produce a PDF (%s). Its last words: %s" % (browser, last.strip() or "none"))
    finally:
        shutil.rmtree(profile, ignore_errors=True)
        if os.path.exists(tmp_pdf):
            try:
                os.remove(tmp_pdf)
            except OSError:
                pass


def pdf_pages_of_dests(data):
    """({destination name: page number}, page count) of a PDF written by Chrome. Empty on any surprise."""
    try:
        objs = {int(m.group(1)): m.group(2) for m in re.finditer(rb"(\d+) 0 obj\s*(<<.*?>>)\s*(?:stream|endobj)", data, re.S)}
        cat = [b for b in objs.values() if re.search(rb"/Type\s*/Catalog", b)][0]
        root = int(re.search(rb"/Pages (\d+) 0 R", cat).group(1))
        order = []

        def walk(n):
            body = objs[n]
            if re.search(rb"/Type\s*/Pages", body):
                for kid in re.findall(rb"(\d+) 0 R", re.search(rb"/Kids\s*\[(.*?)\]", body, re.S).group(1)):
                    walk(int(kid))
            else:
                order.append(n)

        walk(root)
        dests = {}
        m = re.search(rb"/Dests (\d+) 0 R", cat)
        if m:
            for name, page in re.findall(rb"/([^\s/\[\]<>]+)\s*\[\s*(\d+) 0 R", objs[int(m.group(1))]):
                if int(page) in order:
                    dests[name.decode("latin-1")] = order.index(int(page)) + 1
        return dests, len(order)
    except Exception:  # noqa: BLE001
        return {}, 0


# --------------------------------------------------------------------------------------------
def build_html(d, css, stacks, toc_pages):
    global _uid
    _uid = [0]
    text = re.sub(r"<!--.*?-->", "", read(os.path.join(MANUAL, "manual.md")), flags=re.S)
    values = {"version": d.version, "vendor": d.vendor, "url": d.url, "controls": str(len(d.settings)),
              "pages": str(len(d.pages)), "programs": str(len(d.programs))}

    def sub(m):
        if m.group(1) not in values:
            die("manual.md: unknown {{%s}} (known: %s)" % (m.group(1), ", ".join(sorted(values))))
        return values[m.group(1)]

    text = re.sub(r"\{\{(\w+)\}\}", sub, text)
    book = Book(d)
    book.toc_pages = toc_pages
    body = book.render(text)
    body = body.replace("<!--TOC-->", book.toc_html())
    title = "VHS Vibes %s - User Manual" % d.version
    page = ('<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><title>%s</title>\n'
            '<meta name="author" content="%s">\n<!-- Written by tools/make_manual.py. Do not edit: change manual/*.md and run it again. -->\n'
            '<style>\n%s\n</style></head><body>\n%s\n</body></html>\n' % (esc(title), esc(d.vendor), css, body))
    return page, book


def checks_on_html(d, page):
    plain = html.unescape(re.sub(r"<[^>]+>", " ", re.sub(r"<style>.*?</style>", " ", page, flags=re.S)))
    errors = []
    for p in d.settings:
        if not re.search(r"(?<![\w])" + re.escape(p["name"]) + r"(?![\w])", plain):
            errors.append("the parameter name '%s' does not appear in the manual" % p["name"])
    n = len(re.findall("D" + "olby", plain))
    if n > 0:
        errors.append("the trade name of the noise reduction's maker appears %d times; it is a trademark and must not "
                      "appear. Write 'NR' or 'B-type noise reduction'" % n)
    if errors:
        die(errors)


def main():
    global PAGE_COLOURS, FONT_LOGO_SVG, FONT_BODY_SVG, FONT_GOTHIC_SVG
    ap = argparse.ArgumentParser(description="Build the VHS Vibes user manual (manual/VHS-Vibes-Manual.pdf).")
    ap.add_argument("--html-only", action="store_true", help="write the HTML and skip the PDF")
    ap.add_argument("--browser", help="Chrome / Edge / Chromium executable to print with")
    args = ap.parse_args()

    d = load()
    mk = d.mk
    stacks, font_report = font_stacks()
    FONT_LOGO_SVG, FONT_BODY_SVG, FONT_GOTHIC_SVG = (stacks[k].replace('"', "'") for k in ("logo", "body", "gothic"))
    on_paper = [rgb(mk.PINK), "#0a9fb0", rgb(mk.ORANGE), rgb(mk.PURPLE), rgb(mk.INK), "#7a7a00"]
    PAGE_COLOURS = {p["name"]: on_paper[i % len(on_paper)] for i, p in enumerate(d.pages)}

    tokens = {"pink": rgb(mk.PINK), "pink_hi": rgb(mk.PINK_HI), "pink_lo": rgb(mk.PINK_LO), "cyan": rgb(mk.CYAN),
              "purple": rgb(mk.PURPLE), "yellow": rgb(mk.YELLOW), "orange": rgb(mk.ORANGE), "night": rgb(mk.NIGHT),
              "panel_top": rgb(mk.PANEL_TOP), "panel_bot": rgb(mk.PANEL_BOT), "paper": rgb(mk.PAPER), "ink": rgb(mk.INK),
              "f_gothic": stacks["gothic"], "f_head": stacks["head"], "f_logo": stacks["logo"], "f_body": stacks["body"],
              "f_mono": stacks["mono"], "footer_left": "ＶＨＳ　ＶＩＢＥＳ　／　" + wide("v" + d.version)}
    css = read(os.path.join(MANUAL, "manual.css"))

    def tok(m):
        if m.group(1) not in tokens:
            die("manual.css: unknown {{%s}}" % m.group(1))
        return tokens[m.group(1)]

    css = re.sub(r"\{\{(\w+)\}\}", tok, css)

    page, book = build_html(d, css, stacks, {})
    checks_on_html(d, page)
    with open(OUT_HTML, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    print("make_manual: wrote %s" % os.path.relpath(OUT_HTML, PROJECT))

    pages = 0
    if not args.html_only:
        browser = find_browser(args.browser)
        data = print_pdf(browser, OUT_HTML, OUT_PDF)
        dests, pages = pdf_pages_of_dests(data)
        toc = {ch["id"]: str(dests[ch["id"]]) for ch in book.chapters if ch["id"] in dests}
        wanted = [ch["id"] for ch in book.chapters if ch.get("toc") != "-"]
        if all(w in toc for w in wanted):
            # second pass: the same pages, now with their numbers in the contents list
            page, book = build_html(d, css, stacks, toc)
            with open(OUT_HTML, "w", encoding="utf-8", newline="\n") as f:
                f.write(page)
            data = print_pdf(browser, OUT_HTML, OUT_PDF)
            dests2, pages = pdf_pages_of_dests(data)
            if any(str(dests2.get(w)) != toc[w] for w in wanted):
                warn("the page numbers in the contents list moved between the two passes: run again")
        else:
            warn("could not read the chapter pages back from the PDF: the contents list has no page numbers")
        print("make_manual: wrote %s (%d pages, %.1f MB) with %s"
              % (os.path.relpath(OUT_PDF, PROJECT), pages, os.path.getsize(OUT_PDF) / 1048576.0, browser))

    print("make_manual: version %s, %d controls on %d pages, %d programs" % (d.version, len(d.settings), len(d.pages), len(d.programs)))
    print("make_manual: fonts: " + "; ".join(font_report))
    if WARNINGS:
        print("make_manual: %d warning(s), listed above." % len(WARNINGS))


if __name__ == "__main__":
    main()
