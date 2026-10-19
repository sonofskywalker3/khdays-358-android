#!/usr/bin/env python3
"""The settings menu as the HD 1.5+2.5 collection draws its own, in Days's colours, from your own copy
(SETTINGS: docs/HD_SCREENS.md, "Making them"), into mods/NAME/layouts/settings/<lang>/.
  python tools/hd/theater_settings.py SETTINGS [--mod NAME] [--lang es|en|fr|de|it ...] [--preview DIR]
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import theater_title as tt  # noqa: E402

LANGUAGES = {"es": "sp", "en": "uk", "fr": "fr", "de": "gr", "it": "it"}
LAYOUTS = "l2d/setting_1525.l2d"
PICTURE = (1920, 1080)
FRAME = 30.0  # the frames the pieces are taken at: their appearing done
SHOWN, DIMMED, SELECTED = 0, 3, 4
CHOSEN = 6  # a section's tab: the chosen one, moved right (4 moves it there, 5 flashes it)
# The pieces: name, layout, node, the controls taken (state name: control), each part's role.
PIECES = [
    ("section", 700, 701, {"shown": SHOWN, "selected": CHOSEN}),
    ("row", 750, 751, {"shown": SHOWN, "dimmed": DIMMED, "selected": SELECTED}),
    ("switch", 850, 851, {"shown": SHOWN, "dimmed": DIMMED, "selected": SELECTED}),
    ("check", 850, 862, {"shown": SHOWN}),
    ("number", 950, 951, {"shown": SHOWN, "dimmed": DIMMED, "selected": SELECTED}),
    ("slider", 950, 962, {"shown": SHOWN, "selected": SELECTED}),
    ("arrow_left", 1020, 1027, {"shown": SHOWN}),
    ("arrow_right", 1020, 1028, {"shown": SHOWN}),
    ("question", 650, 661, {"shown": SHOWN}),
    ("answer", 650, 662, {"shown": SHOWN, "selected": SELECTED}),
    ("hints", 1010, 340, {"shown": SHOWN}),
    ("line", 150, 181, {"shown": SHOWN}),
]
SCREEN = (100, (101, 102))  # the screen's nodes, drawn into one picture
# 103's five parts: the faded picture of the section shown (the game shows one), in their order
EMBLEMS = (100, 103, ("emblem_screen", "emblem_sound", "emblem_pad", "emblem_keyboard", "emblem_film"))
# the devices' pictures: 400's 411 shows, in its control 4, a PlayStation, an Xbox and another
# gamepad; 350's 361 the keyboard (12 parts drawn together), 300's 329 the mouse
PADS = (400, 411, 4, ("pad_playstation", "pad_xbox", "pad_other"))
DEVICES = [("keyboard", 350, 361, SHOWN), ("mouse", 300, 329, SHOWN)]


# --- The colours --------------------------------------------------------------------------------


def greys(a: np.ndarray) -> np.ndarray:
    """Each colour as the grey of its brightness (its largest channel)."""
    out = a.copy()
    v = a[..., :3].max(axis=2)
    for c in range(3):
        out[..., c] = v
    return out


KEPT = {("check", "whole"), ("question", "title")}  # red as they are: Days's red


def red_and_silver(a: np.ndarray) -> np.ndarray:
    """A selected piece: its gold red (as the new game's selected bar, darker), its bright rim
    silver."""
    out = a.copy().astype(np.float32)
    rgb = a[..., :3].astype(np.float32)
    v = rgb.max(axis=2)
    s = np.where(v > 0, (v - rgb.min(axis=2)) / np.maximum(v, 1), 0)
    rim = (v > 200) | (s < 0.35)
    red = np.stack([v * 1.0, v * 0.07, v * 0.07], axis=2)
    grey = np.stack([v, v, v], axis=2) * 1.05
    out[..., :3] = np.where(rim[..., None], grey, red)
    # the gold is half seen through over the shown piece: the red more solid, as the new game's
    out[..., 3] = np.where(rim, out[..., 3], np.minimum(255, out[..., 3] * 1.6))
    return np.clip(out, 0, 255).astype(np.uint8)


# --- The pieces ---------------------------------------------------------------------------------


def part_pixels(layouts: tt.Layouts, p) -> np.ndarray:
    """A part as RGBA at its place's size: its texels, tinted by its colours."""
    _, base, ua, va, ub, vb = p["region"]
    tex = layouts.blocks[base].texture[va:vb, ua:ub].astype(np.float32)
    x0, y0, x1, y1 = p["rect"]
    w, h = max(1, int(round(x1 - x0))), max(1, int(round(y1 - y0)))
    tint = np.array(p["corners"], dtype=np.float32).mean(axis=0) * np.array(p["colour"], dtype=np.float32)
    tex = np.clip(tex * tint, 0, 255).astype(np.uint8)
    im = Image.fromarray(tex, "RGBA").resize((w, h), Image.LANCZOS)
    fx, fy = p["flip"]
    if fx:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
    if fy:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    return np.asarray(im)


def node_ids(layouts: tt.Layouts, layout: int):
    mf, nidx, ncount, *_ = layouts.layouts[layouts.ids.index(layout)]
    return [layouts.nodes[nidx + k][12] for k in range(ncount)]


def parts_of(layouts: tt.Layouts, layout: int, node: int, control: int):
    controls = {n: (control if n == node else None) for n in node_ids(layouts, layout)}
    return [p for p in layouts.pieces(layout, FRAME, controls, {}) if p["region"][0] == "texture"]


def screen(layouts: tt.Layouts) -> np.ndarray:
    """The screen's nodes drawn in order into one picture, over black."""
    out = np.zeros((PICTURE[1], PICTURE[0], 4), dtype=np.float32)
    out[..., 3] = 255
    layout, nodes = SCREEN
    for node in nodes:
        for p in parts_of(layouts, layout, node, SHOWN):
            px = part_pixels(layouts, p).astype(np.float32)
            x0, y0 = int(round(p["rect"][0])), int(round(p["rect"][1]))
            sx0, sy0 = max(0, -x0), max(0, -y0)
            x0, y0 = max(0, x0), max(0, y0)
            w = min(px.shape[1] - sx0, PICTURE[0] - x0)
            h = min(px.shape[0] - sy0, PICTURE[1] - y0)
            if w <= 0 or h <= 0:
                continue
            src = px[sy0 : sy0 + h, sx0 : sx0 + w]
            dst = out[y0 : y0 + h, x0 : x0 + w]
            a = src[..., 3:4] / 255.0
            if p["blend"] == 1:  # added
                dst[..., :3] = np.minimum(255, dst[..., :3] + src[..., :3] * a)
            else:
                dst[..., :3] = dst[..., :3] * (1 - a) + src[..., :3] * a
    return out.astype(np.uint8)


def together(layouts: tt.Layouts, parts):
    """Parts drawn into one picture where they lie: it, and its place."""
    x0 = min(p["rect"][0] for p in parts)
    y0 = min(p["rect"][1] for p in parts)
    x1 = max(p["rect"][2] for p in parts)
    y1 = max(p["rect"][3] for p in parts)
    out = Image.new("RGBA", (int(round(x1 - x0)), int(round(y1 - y0))), (0, 0, 0, 0))
    for p in parts:
        out.alpha_composite(
            Image.fromarray(part_pixels(layouts, p), "RGBA"),
            (int(round(p["rect"][0] - x0)), int(round(p["rect"][1] - y0))),
        )
    return np.asarray(out), (x0, y0, x1, y1)


def roles(name: str, parts):
    """Each part's role: a three-part piece's left, middle and right by their places; the
    slider's track ends, track, filled part and knob."""
    if name == "slider":
        by_x = sorted(parts, key=lambda p: p["rect"][2] - p["rect"][0], reverse=True)
        track = by_x[0]
        rest = [p for p in parts if p is not track]
        ends = sorted([p for p in rest if p["rect"][1] == track["rect"][1]], key=lambda p: p["rect"][0])
        others = [p for p in rest if p not in ends]
        fill = max(others, key=lambda p: p["rect"][2] - p["rect"][0])
        knob = [p for p in others if p is not fill]
        out = [("track", track)] + list(zip(("left", "right"), ends)) + [("fill", fill)] + [("knob", k) for k in knob]
        return out
    if name == "question":
        frame = [p for p in parts if p["rect"][3] - p["rect"][1] > 100]
        title = [p for p in parts if p not in frame]
        frame.sort(key=lambda p: p["rect"][0])
        return list(zip(("left", "middle", "right"), frame)) + [("title", t) for t in title]
    lefts = {round(p["rect"][0]) for p in parts}
    if len(parts) == 3 and len(lefts) == 1:  # stacked: the line between the sections and the rows
        return list(zip(("top", "middle", "bottom"), sorted(parts, key=lambda p: p["rect"][1])))
    if len(parts) == 3:
        return list(zip(("left", "middle", "right"), sorted(parts, key=lambda p: p["rect"][0])))
    if len(parts) == 2 and len(lefts) == 2:  # side by side: the hints' bar's halves
        return list(zip(("left", "right"), sorted(parts, key=lambda p: p["rect"][0])))
    return [("whole", p) for p in parts]  # one, or drawn over each other


# --- The font -----------------------------------------------------------------------------------


def font(folder: Path):
    cod = (folder / "font" / "menufont.cod").read_bytes()
    glyphs = []
    for i in range(len(cod) // 8):
        code, x, y, _, w = struct.unpack_from("<HHHBB", cod, 8 * i)
        glyphs.append((code, x, y, w))
    picture = tt.tim2_picture((folder / "font" / "menufont.tm2").read_bytes(), 0)
    return glyphs, picture[..., 3].copy()


# --- The files ----------------------------------------------------------------------------------


def make(settings: Path, lang: str, out_dir: Path, preview: Path | None):
    folder = settings / LANGUAGES[lang]
    layouts = tt.Layouts(folder / LAYOUTS)
    atlas = tt.Atlas()
    lines = [
        "# The settings menu as KINGDOM HEARTS HD 1.5+2.5 ReMIX draws its own, in Days's colours, made by",
        "# tools/hd/theater_settings.py from the player's own copy (host/settings/settings_menu.cpp reads it):",
        "# the screen, then each piece's parts (piece, state, role, place in the picture, texels in the",
        "# atlas), then the font's glyphs (code, place in the font's picture, width; cells of 32).",
        f"picture {PICTURE[0]} {PICTURE[1]}",
        "atlas settings_atlas.png",
        "font settings_font.png 32",
    ]
    shown = greys(screen(layouts))
    atlas.add(("screen",), shown)
    entries = []
    for name, layout, node, states in PIECES:
        for state, control in states.items():
            parts = parts_of(layouts, layout, node, control)
            if not parts:
                print(f"{lang}: {name} {state}: no parts")
                continue
            for role, p in roles(name, parts):
                px = part_pixels(layouts, p)
                px = px if (name, role) in KEPT else red_and_silver(px) if state == "selected" else greys(px)
                key = (name, state, role, len(entries))
                atlas.add(key, px)
                entries.append((key, p["rect"]))
    layout, node, names = EMBLEMS
    for name, p in zip(names, parts_of(layouts, layout, node, SHOWN)):
        key = (name, "shown", "whole", len(entries))
        atlas.add(key, greys(part_pixels(layouts, p)))
        entries.append((key, p["rect"]))
    layout, node, control, names = PADS
    for name, p in zip(names, parts_of(layouts, layout, node, control)):
        key = (name, "shown", "whole", len(entries))
        atlas.add(key, greys(part_pixels(layouts, p)))
        entries.append((key, p["rect"]))
    for name, layout, node, control in DEVICES:
        picture, rect = together(layouts, parts_of(layouts, layout, node, control))
        key = (name, "shown", "whole", len(entries))
        atlas.add(key, greys(picture))
        entries.append((key, rect))
    pixels = atlas.pack()
    sx, sy = atlas.at[("screen",)]
    lines.append(f"screen 0 0 {PICTURE[0]} {PICTURE[1]} {sx} {sy}")
    for key, rect in entries:
        name, state, role, _ = key
        u, v = atlas.at[key]
        h, w = atlas.pixels[key].shape[:2]
        x0, y0 = rect[0], rect[1]
        lines.append(f"part {name} {state} {role} {x0:.1f} {y0:.1f} {w} {h} {u} {v}")
    glyphs, sheet = font(folder)
    for code, x, y, w in glyphs:
        lines.append(f"glyph {code} {x} {y} {w}")
    out_dir.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels, "RGBA").save(out_dir / "settings_atlas.png")
    Image.fromarray(sheet, "L").save(out_dir / "settings_font.png")
    (out_dir / "settings.txt").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    print(f"{lang}: {len(entries)} parts, atlas {pixels.shape[1]} x {pixels.shape[0]}, {len(glyphs)} glyphs")
    if preview is not None:
        preview.mkdir(parents=True, exist_ok=True)
        Image.fromarray(pixels, "RGBA").save(preview / f"settings_atlas_{lang}.png")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("settings", type=Path, help="<collection>/Image/SettingMenu.hed_out/original")
    ap.add_argument("--mod", default="Theater title")
    ap.add_argument("--lang", nargs="*", default=list(LANGUAGES), choices=list(LANGUAGES))
    ap.add_argument("--preview", type=Path)
    args = ap.parse_args()
    for lang in args.lang:
        make(args.settings, lang, tt.ROOT / "mods" / args.mod / "layouts" / "settings" / lang, args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
