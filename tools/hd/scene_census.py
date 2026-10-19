#!/usr/bin/env python3
"""The game's pictures of its event scenes: for each scene asked, one headless run of the
port that plays it on demand (a new game's day 7, its first scene path written over:
KHDAYS_TEST_DAY=7, KHDAYS_TEST_SCENE=<slot>:<entry>), its lines answered with A every 45 frames, its
pictures saved (KHDAYS_SCENE_SHOTS) and its course traced (KHDAYS_TRACE_SCENES). One run at a time;
a scene already done (its log says the event closed) is not run again.

  python tools/hd/scene_census.py OUT EV_S#4 EV_S#6 ...     # or --shared: every EV_S entry with a stretch
      [--exe build/port/obj-release-x64/khdays-native.exe] [--nitrofs data/extracted/<hash>/nitrofs]

OUT/shots holds the pictures (EV_S_4_02400.bmp), OUT/logs a log per scene, OUT/census.json what each
run saw. The executable is copied into OUT first, so the port can be built while the census runs.
Only the shared archive's scenes (slot 4) are played so: a world's scene (slot 5) needs a mission of
its world, not done here.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ds_scripts as ds  # noqa: E402

# The story walk's start (port/tools/regress_walks/story.input) to the first field, then A every 45
# frames: through day 7's event with the scene in place of its first.
PREFIX = (
    "uOv000_TickMenuLoop 60+A 66-A 260+A 266-A 460+A 466-A 610+LEFT 616-LEFT 670+A 676-A s11 400+START 410-START s2 "
)
PRESSES = 400
TIMEOUT = 420  # seconds a run may take; the port leaves on its own when the event closes


def walk() -> str:
    return PREFIX + " ".join(f"{200 + 45 * i}+A {204 + 45 * i}-A" for i in range(PRESSES)) + "\n"


def shared_scenes(nitrofs: Path) -> list[str]:
    """Every EV_S entry with a skip target set (a stretch to replace)."""
    census = ds.census(nitrofs)
    keys = [k for k, items in census.items() if k.startswith("EV_S#") and any(i.get("target", -1) != -1 for i in items)]
    return sorted(keys, key=lambda k: int(k.split("#")[1]))


def read_log(path: Path) -> dict:
    """What a run's log says: the scenes started (frame, key), the event's closing, the leaving."""
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    started = [(int(f), k) for f, k in re.findall(r"scene: f(\d+) start (\S+?):", text)]
    closed = [int(f) for f in re.findall(r"scene: f(\d+) status 000a", text)]
    return {
        "started": started,
        "closed": closed[0] if closed else None,
        "left": "KHDAYS_TEST_SCENE: the scene ended; leaving" in text,
        "replaced": re.findall(r'KHDAYS_TEST_SCENE: the event\'s "([^"]*)" becomes "([^"]*)"', text),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out", type=Path)
    ap.add_argument("keys", nargs="*")
    ap.add_argument("--shared", action="store_true", help="every EV_S entry with a stretch")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "port" / "obj-release-x64" / "khdays-native.exe")
    ap.add_argument("--nitrofs", type=Path)
    args = ap.parse_args()
    keys = list(args.keys)
    if args.shared:
        nitrofs = args.nitrofs or next((ROOT / "data" / "extracted").glob("*/nitrofs"))
        keys += [k for k in shared_scenes(nitrofs) if k not in keys]
    bad = [k for k in keys if not re.fullmatch(r"EV_S#\d+", k)]
    if bad or not keys:
        ap.error(f"scenes of the shared archive (EV_S#<n>) only: {bad or 'none given'}")
    out = args.out.resolve()
    (out / "shots").mkdir(parents=True, exist_ok=True)
    (out / "logs").mkdir(exist_ok=True)
    exe = out / "khdays-native.exe"
    shutil.copy2(args.exe, exe)
    if args.exe.with_suffix(".pdb").exists():  # its symbols beside it, so the build's stay free
        shutil.copy2(args.exe.with_suffix(".pdb"), exe.with_suffix(".pdb"))
    (out / "walk.input").write_text(walk(), encoding="utf-8")
    # the game's own scenes, whatever HD videos are installed
    (out / "settings.ini").write_text("[video]\nmovies = ds\n", encoding="utf-8")
    census_path = out / "census.json"
    census = json.loads(census_path.read_text(encoding="utf-8")) if census_path.exists() else {}
    for n, key in enumerate(keys, 1):
        log = out / "logs" / (key.replace("#", "_") + ".log")
        if key in census and census[key].get("left"):
            continue
        entry = key.split("#")[1]
        started = time.monotonic()
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "port" / "tools" / "regress.py"),
                "--input",
                str(out / "walk.input"),
                "--log",
                str(log),
                "--settings",
                str(out / "settings.ini"),
                "--timeout",
                str(TIMEOUT),
                "--env",
                "KHDAYS_TEST_DAY=7",
                "--env",
                f"KHDAYS_TEST_SCENE=4:{entry}",
                "--env",
                f"KHDAYS_SCENE_SHOTS={out / 'shots'}",
                "--env",
                "KHDAYS_TRACE_SCENES=1",
                "--env",
                f"_NT_SYMBOL_PATH={out}",
                str(exe),
            ],
            capture_output=True,
        )
        result = read_log(log)
        result["pictures"] = len(list((out / "shots").glob(key.replace("#", "_") + "_[0-9]*.bmp")))
        result["seconds"] = round(time.monotonic() - started)
        census[key] = result
        census_path.write_text(json.dumps(census, indent=1), encoding="utf-8")
        print(
            f"[{n}/{len(keys)}] {key}: {result['pictures']} pictures, {result['seconds']} s, "
            f"{'closed' if result['left'] else 'did not close'}",
            flush=True,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
