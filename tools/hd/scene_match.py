#!/usr/bin/env python3
"""Pairs the HD theater's scenes with Days's event scenes by their lines: every theater
scene with a video against every game script with lines (ds_scripts.py), in Spanish and English;
then the path through the event archive EV_S that keeps the story's order and takes the most
agreement; scenes without lines are placed between their neighbours. Candidates for review: the
scene map (port/movies/hd_scenes.txt) is written by hand from them, with the evidence.

  python tools/hd/scene_match.py MARE NITROFS OUT            # OUT/candidates.txt, OUT/scores.json
  python tools/hd/scene_match.py MARE NITROFS --show hd004 EV_S#3 EV_S#4   # lines side by side

MARE is <1.5+2.5>/Image/Mare.hed_out/original; NITROFS the game's extracted files.
"""

from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ds_scripts as ds  # noqa: E402
import theater_data as td  # noqa: E402

LANGS = ("es", "en")
SAME = 0.6  # a line counts when its best match is at least this alike
CANDIDATES = 12  # scripts compared closely per scene, picked by shared words
SHARED_SCRIPT = re.compile(r"EV_S#(\d+)")


def norm(text: str) -> str:
    """Lower case, no accents or punctuation, single spaces."""
    text = unicodedata.normalize("NFKD", text.replace("\n", " ")).encode("ascii", "ignore").decode()
    return re.sub(r"\s+", " ", re.sub(r"[^a-z0-9 ]", " ", text.lower())).strip()


def words(text: str) -> set[str]:
    return {w for w in text.split() if len(w) > 3}


def line_score(theater: list[str], game: list[str]) -> float:
    """The share of the theater's lines found among the game's (each by its best match, if alike)."""
    if not theater or not game:
        return 0.0
    found = 0.0
    for line in theater:
        best = max(difflib.SequenceMatcher(None, line, g).ratio() for g in game)
        found += best if best >= SAME else 0.0
    return found / len(theater)


def game_lines(census: dict[str, list[dict]]) -> dict[str, dict[str, list[str]]]:
    out = {}
    for key, items in census.items():
        per = {lang: [norm(i["line"][lang]) for i in items if "line" in i and i["line"].get(lang)] for lang in LANGS}
        per = {lang: [line for line in lines if len(line) >= 4] for lang, lines in per.items()}
        if any(per.values()):
            out[key] = per
    return out


def scores(theater: dict[str, dict[str, list[str]]], game: dict[str, dict[str, list[str]]]) -> dict[str, list]:
    """Per theater scene: [(score, script)] best first, over the scripts sharing most words."""
    index: dict[str, set[str]] = {}
    for key, per in game.items():
        for w in set().union(*(words(line) for line in per["es"] + per["en"])):
            index.setdefault(w, set()).add(key)
    out = {}
    for scene, per in theater.items():
        votes: dict[str, int] = {}
        for line in per["es"] + per["en"]:
            for w in words(line):
                for key in index.get(w, ()):
                    votes[key] = votes.get(key, 0) + 1
        ranked = []
        for key in sorted(votes, key=lambda k: -votes[k])[:CANDIDATES]:
            both = [line_score(per[lang], game[key][lang]) for lang in LANGS if per[lang]]
            ranked.append((round(sum(both) / len(both), 3), key))
        out[scene] = sorted(ranked, reverse=True)
    return out


def story_path(order: list[str], ranked: dict[str, list]) -> dict[str, tuple[str, float] | None]:
    """The shared archive's (EV_S) entries chosen so their indices never go back along the story,
    taking the most score; a scene keeps none when no choice fits."""
    states: dict[int, tuple[float, list]] = {-1: (0.0, [])}
    for scene in order:
        choices = [
            (int(m.group(1)), s, k) for s, k in ranked.get(scene, []) if s > 0 and (m := SHARED_SCRIPT.fullmatch(k))
        ]
        nxt: dict[int, tuple[float, list]] = {}
        for last, (total, path) in states.items():
            if last not in nxt or nxt[last][0] < total:
                nxt[last] = (total, path + [None])
            for index, s, key in choices:
                if index >= last and (index not in nxt or nxt[index][0] < total + s):
                    nxt[index] = (total + s, path + [(key, s)])
        states = nxt
    _, path = max(states.values(), key=lambda v: v[0])
    return dict(zip(order, path))


def show(mare: Path, nitrofs: Path, scene: str, keys: list[str]) -> None:
    lines = td.lines(mare, "es")
    print(f"== {scene}")
    for first, _, text in lines.get(scene, []):
        print(f"   {first:5d} {(text or '').replace(chr(10), ' ')}")
    census = ds.census(nitrofs)
    for key in keys:
        print(f"== {key}")
        for item in census.get(key, []):
            if "target" in item:
                print(f"   {item['at']:5x} target {item['target']}")
            else:
                print(f"   {item['at']:5x} {item['line'].get('es', '').replace(chr(10), ' ')}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("mare", type=Path)
    ap.add_argument("nitrofs", type=Path)
    ap.add_argument("out", type=Path, nargs="?")
    ap.add_argument("--show", nargs="+", metavar=("SCENE", "SCRIPT"))
    args = ap.parse_args()
    if args.show:
        show(args.mare, args.nitrofs, args.show[0], args.show[1:])
        return 0
    if args.out is None:
        ap.error("OUT is needed without --show")
    story = td.story(args.mare)
    theater_raw = {lang: td.lines(args.mare, lang) for lang in LANGS}
    theater = {}
    for entry in story:
        stem = entry["stem"]
        theater[stem] = {
            lang: [line for line in (norm(t) for _, _, t in theater_raw[lang].get(stem, []) if t) if len(line) >= 4]
            for lang in LANGS
        }
    game = game_lines(ds.census(args.nitrofs))
    ranked = scores({s: p for s, p in theater.items() if p["es"] or p["en"]}, game)
    order = [e["stem"] for e in story if not e["stem"].startswith("hd8")]
    chosen = story_path(order, ranked)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "scores.json").write_text(json.dumps(ranked, indent=1), encoding="utf-8")
    rows = []
    last = "the story's start"
    for entry in story:
        stem = entry["stem"]
        head = f"ch{entry['chapter']:3d} day{entry['day']:3d} {stem:7s}"
        if stem.startswith("hd8"):
            rows.append(f"{head} DS movie {stem[2:]}")
            continue
        pick = chosen.get(stem)
        others = [f"{k} {s}" for s, k in ranked.get(stem, [])[:3] if pick is None or k != pick[0]]
        if pick is not None:
            rows.append(f"{head} {pick[0]} ({pick[1]})" + (f"; also {', '.join(others[:2])}" if others else ""))
            last = pick[0]
        else:
            n = len(theater.get(stem, {}).get("es", []))
            rows.append(f"{head} ? after {last}, {n} lines" + (f"; best {', '.join(others[:2])}" if others else ""))
    (args.out / "candidates.txt").write_text("\n".join(rows) + "\n", encoding="utf-8")
    paired = sum(1 for e in story if chosen.get(e["stem"]) is not None)
    print(f"{len(story)} theater scenes with a video: {paired} paired by lines, the rest for review -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
