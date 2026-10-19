#!/usr/bin/env python3
"""Comparison pictures of the HD scenes: for each scene of the map, a row of the game's
pictures (KHDAYS_SCENE_SHOTS: <scene>_<frame>.bmp, both screens) over a row of the HD video's at the
same fractions of the scene, captioned with the scene, its videos and the map's evidence. For the
review; written under OUT, never into an install.

  python tools/hd/scene_compare.py SHOTS VIDEOS OUT [--map port/movies/hd_scenes.txt] [--only EV_S#4 ...]

SHOTS holds the game's pictures; VIDEOS the HD videos (data/cutscenes/cinematics). The HD pictures are
taken from the videos with ffmpeg (a developer's tool, not the port's: found on PATH).
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
COLUMNS = 8
CELL_W, CELL_H = 256, 192  # one DS screen; the video's picture fitted to it
CAPTION = 28


def map_entries(path: Path) -> list[tuple[str, list[str], str]]:
    """(key, stems, evidence) of the map's entries that play videos."""
    out = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = re.sub(r"(^|\s)#.*$", "", raw)
        line, _, evidence = line.partition(";")
        words = line.split()
        if len(words) >= 2 and words[1] != "off":
            out.append((words[0], words[1:], evidence.strip()))
    return out


def ds_pictures(shots: Path, key: str) -> list[Path]:
    name = re.sub(r"[#/:]", "_", key)
    return sorted(shots.glob(f"{name}_[0-9][0-9][0-9][0-9][0-9].bmp"))


def picked(pictures: list[Image.Image]) -> list[Image.Image]:
    """COLUMNS of the pictures at equal fractions, leaving out the black ones (fades) when there are
    enough others."""
    lit = [p for p in pictures if max(p.convert("L").getextrema()) > 16]
    pool = lit if len(lit) >= COLUMNS else pictures
    if not pool:
        return []
    return [pool[min(len(pool) - 1, round(i * (len(pool) - 1) / max(1, COLUMNS - 1)))] for i in range(COLUMNS)]


def video_seconds(ffprobe: str, video: Path) -> float:
    out = subprocess.run(
        [ffprobe, "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(video)],
        capture_output=True,
        text=True,
    ).stdout
    return float(out.strip() or 0)


def video_picture(ffmpeg: str, video: Path, at: float, work: Path) -> Image.Image | None:
    target = work / "frame.png"
    subprocess.run(
        [ffmpeg, "-v", "error", "-y", "-ss", f"{at:.3f}", "-i", str(video), "-frames:v", "1", str(target)],
        capture_output=True,
    )
    if not target.exists():
        return None
    picture = Image.open(target).convert("RGB")
    target.unlink()
    return picture


def fitted(picture: Image.Image) -> Image.Image:
    cell = Image.new("RGB", (CELL_W, CELL_H))
    scale = min(CELL_W / picture.width, CELL_H / picture.height)
    small = picture.resize((max(1, round(picture.width * scale)), max(1, round(picture.height * scale))))
    cell.paste(small, ((CELL_W - small.width) // 2, (CELL_H - small.height) // 2))
    return cell


def sheet(key: str, stems: list[str], evidence: str, ds: list[Image.Image], hd: list[Image.Image]) -> Image.Image:
    out = Image.new("RGB", (COLUMNS * CELL_W, CAPTION + 2 * CELL_H), (24, 24, 24))
    draw = ImageDraw.Draw(out)
    draw.text((8, 8), f"{key}  ->  {' '.join(stems)}    ({evidence})", fill=(240, 240, 240))
    for i, picture in enumerate(ds[:COLUMNS]):
        out.paste(fitted(picture), (i * CELL_W, CAPTION))
    for i, picture in enumerate(hd[:COLUMNS]):
        out.paste(fitted(picture), (i * CELL_W, CAPTION + CELL_H))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("shots", type=Path)
    ap.add_argument("videos", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--map", type=Path, default=ROOT / "port" / "movies" / "hd_scenes.txt")
    ap.add_argument("--only", nargs="*")
    args = ap.parse_args()
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        print("scene_compare: ffmpeg and ffprobe are needed on PATH for the videos' pictures")
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    made = 0
    with tempfile.TemporaryDirectory() as t:
        work = Path(t)
        for key, stems, evidence in map_entries(args.map):
            if args.only and key not in args.only:
                continue
            shots = ds_pictures(args.shots, key)
            ds = picked([Image.open(p).convert("RGB").crop((0, 0, 256, 192)) for p in shots])  # the upper screen
            videos = [args.videos / f"{s}.mp4" for s in stems]
            lengths = [video_seconds(ffprobe, v) if v.exists() else 0.0 for v in videos]
            total = sum(lengths)
            hd = []
            for i in range(COLUMNS):
                at = (i + 0.5) / COLUMNS * total
                for video, length in zip(videos, lengths):
                    if at < length:
                        picture = video_picture(ffmpeg, video, at, work)
                        if picture is not None:
                            hd.append(picture)
                        break
                    at -= length
            name = re.sub(r"[#/:]", "_", key)
            sheet(key, stems, evidence, ds, hd).save(args.out / f"{name}.png")
            made += 1
            print(f"{key}: {len(shots)} game pictures, {len(hd)} of the video's")
    print(f"{made} comparison pictures in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
