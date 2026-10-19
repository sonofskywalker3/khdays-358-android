#!/usr/bin/env python3
"""The theater's videos and where the port plays them: for every video of the player's
collection, the game's scene it plays in (the scene map), the day's monologue it is the voice of, the
game's movie it replaces (hd8xx, DOP as 802, DED as 843), or that it is not played; and for every
entry of the map, that its videos are the collection's and its script the game's: an event archive's
entry or a mission's script section that sets a skip target, or a monologue's text record.

  python tools/hd/scene_report.py MARE VIDEOS NITROFS [--map port/movies/hd_scenes.txt]

MARE is `<collection>/Image/Mare.hed_out/original`, VIDEOS the theater's videos (STEAM/Mare/MOVIE/Days/dt
or EPIC/Mare/MOVIE/Days/en), NITROFS the game's extracted files. Exit 1 when the map names a video the
collection lacks or a script the game lacks.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ds_scripts as ds  # noqa: E402
import scene_compare as sc  # noqa: E402
import theater_data as td  # noqa: E402
from decompress_data import decompress_nitro  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
MOVIES = {"DOP": "802", "DED": "843"}  # the collection's names of two of the game's movies
NOT_DAYS = {"CED": "Re:coded's ending, not Days's"}


def movie_of(stem: str) -> str | None:
    """The game's movie a video stands for (`hd818` -> 818), or None for a scene's."""
    if stem in MOVIES:
        return MOVIES[stem]
    m = re.fullmatch(r"hd(8\d\d)", stem)
    return m.group(1) if m else None


def game_movies(nitrofs: Path) -> set[str]:
    """The game's movies: mv/<id>.mods, or one per language, mv/<id>_<lang>.mods."""
    return {p.stem.split("_")[0] for p in (nitrofs / "mv").glob("*.mods")}


def monologue_records(nitrofs: Path) -> int:
    """How many texts ui/mnl holds (the English file's count, at +4)."""
    path = nitrofs / "UI" / "mnl" / "mnl_en.s.z"
    data = path.read_bytes()
    try:
        data = decompress_nitro(data)[1]
    except ValueError:
        pass  # stored plain
    return struct.unpack_from("<I", data, 4)[0]


def scripts_with_stretch(nitrofs: Path) -> set[str]:
    """The game's scripts (event archive entries, mission script sections) that set a skip target."""
    return {key for key, items in ds.census(nitrofs).items() if any("target" in i for i in items)}


def check(entries, videos: set[str], scripts: set[str], monologues: int) -> list[str]:
    """What is wrong with the map's entries: a video the collection lacks, a script the game lacks."""
    errors = []
    for key, stems, _ in entries:
        m = re.fullmatch(r"mnl#(\d+)", key)
        if m is not None:
            if int(m.group(1)) >= monologues:
                errors.append(f"{key}: the game has {monologues} monologue texts")
        elif key not in scripts:
            errors.append(f"{key}: not a script of the game with a skippable stretch")
        errors += [f"{key}: {stem} is not one of the collection's videos" for stem in stems if stem not in videos]
    return errors


def coverage(videos: list[str], entries, movies: set[str]) -> dict[str, str]:
    """Where each video plays: `scene EV_S#4`, `monologue mnl#0`, `movie 818`, or `not played: why`."""
    played = {}
    for key, stems, _ in entries:
        for stem in stems:
            played.setdefault(stem, ("monologue " if key.startswith("mnl#") else "scene ") + key)
    out = {}
    for stem in videos:
        movie = movie_of(stem)
        if stem in played:
            out[stem] = played[stem]
        elif stem in NOT_DAYS:
            out[stem] = "not played: " + NOT_DAYS[stem]
        elif movie is not None:
            out[stem] = f"movie {movie}" if movie in movies else f"not played: the game has no movie {movie}"
        else:
            out[stem] = "not played: no scene of the game paired with it"
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("mare", type=Path)
    ap.add_argument("videos", type=Path)
    ap.add_argument("nitrofs", type=Path)
    ap.add_argument("--map", type=Path, default=ROOT / "port" / "movies" / "hd_scenes.txt")
    args = ap.parse_args()
    videos = sorted(p.stem for p in args.videos.glob("*.mp4"))
    entries = sc.map_entries(args.map)
    days = {}
    for scene in td.story(args.mare):
        days.setdefault(scene["stem"], scene["day"])
    where = coverage(videos, entries, game_movies(args.nitrofs))
    for stem in videos:
        day = f"day {days[stem]}" if stem in days else "-"
        print(f"{stem:8} {day:8} {where[stem]}")
    kinds = {}
    for w in where.values():
        kind = w.split(":")[0].split()[0] if not w.startswith("not played") else "not played"
        kinds[kind] = kinds.get(kind, 0) + 1
    print(f"{len(videos)} videos: " + ", ".join(f"{n} {k}" for k, n in sorted(kinds.items())))
    errors = check(entries, set(videos), scripts_with_stretch(args.nitrofs), monologue_records(args.nitrofs))
    for e in errors:
        print("map:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
