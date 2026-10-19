#!/usr/bin/env python3
"""Run the port's fuzzers for as long as asked, from their kept inputs in port/fuzz/<name>/, a crash's or
hang's input copied back there; a run that ends well before its time fails (docs/BUILD.md).
    python port/tools/fuzz.py [--tree build/port/obj-release-x64] [--target NAME] [--minutes N] [--jobs N]
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGETS = ["server", "portmap", "session", "mods"]


def run_one(tree: Path, name: str, minutes: float, jobs: int) -> int:
    exe = tree / f"khdays-fuzz-{name}.exe"
    kept = ROOT / "port" / "fuzz" / name
    if not exe.exists():
        sys.exit(f"{exe} does not exist: build the Release tree (port/build.py --config Release)")
    kept.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix=f"khdays-fuzz-{name}-"))
    try:
        shutil.copy2(exe, scratch / exe.name)
        for dll in tree.glob("clang_rt.asan*.dll"):
            shutil.copy2(dll, scratch / dll.name)
        corpus = scratch / "corpus"
        corpus.mkdir()
        for item in kept.iterdir():
            if item.is_file() and not item.name.endswith(".txt"):
                shutil.copy2(item, corpus / item.name)
        (scratch / "artifacts").mkdir()
        seconds = int(minutes * 60)
        print(f"== {name}: {minutes:g} minutes on {jobs} processes", flush=True)
        began = time.monotonic()
        run = subprocess.run(
            [
                str(scratch / exe.name),
                "corpus",
                f"-max_total_time={seconds}",
                f"-jobs={jobs}",
                f"-workers={jobs}",
                "-timeout=10",
                "-rss_limit_mb=2048",
                "-artifact_prefix=artifacts/",
            ],
            cwd=scratch,
            capture_output=True,
            text=True,
            errors="replace",
        )
        took = time.monotonic() - began
        out = run.stdout + run.stderr
        for log in sorted(scratch.glob("fuzz-*.log")):
            out += log.read_text(encoding="utf-8", errors="replace")
        reports = [line for line in out.splitlines() if "ERROR:" in line or "SUMMARY:" in line]
        found = sorted((scratch / "artifacts").iterdir())
        for item in found:
            shutil.copy2(item, kept / item.name)
        runs = sum(int(m) for m in re.findall(r"Done (\d+) runs", out))
        if reports or found:
            print("\n".join(reports[:20]))
            print("kept:", ", ".join(p.name for p in found))
            return 1
        if took < seconds * 0.9:
            print(f"{name}: ended after {took:.0f} s of {seconds}: it did not fuzz")
            print(out[-1500:])
            return 1
        print(f"{name}: no report in {took / 60:.0f} minutes, {runs} runs")
        return 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", type=Path, default=ROOT / "build" / "port" / "obj-release-x64")
    ap.add_argument("--target", choices=TARGETS, action="append")
    ap.add_argument("--minutes", type=float, default=60.0)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = ap.parse_args()
    failed = 0
    for name in args.target or TARGETS:
        failed += run_one(args.tree.resolve(), name, args.minutes, args.jobs)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
