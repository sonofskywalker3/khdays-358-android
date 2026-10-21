"""The HD videos at 60 frames a second: each of data/cutscenes/cinematics/<name>.mp4 made again as
data/cutscenes/cinematics60/<name>.mp4, with a picture drawn between each two of its own by RIFE
(rife-ncnn-vulkan, on the graphics card), its sound copied. Movies = hd-smooth plays them.

It takes hours (about four times the videos' length on a recent graphics card) and can be stopped at
any time: a video is put in place only when it is whole, and the next run goes on from the first one
not made. A video is cut into pieces of PIECE pictures for RIFE, so the pictures on the disk at once
stay a few gigabytes.

RIFE comes with the installer (setup/rife); ffmpeg, which reads and writes the videos, is fetched once
into setup/ffmpeg (FFMPEG_URL, used only when its SHA-256 matches). Nothing is sent anywhere: the
videos are made from the player's own and stay on this computer.

  python smooth_movies.py [--only hd802] [--rife DIR] [--ffmpeg DIR]
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zipfile
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "data" / "cutscenes" / "cinematics"
OUT = ROOT / "data" / "cutscenes" / "cinematics60"
WORK = ROOT / "setup" / "smooth-work"
RIFE = ROOT / "setup" / "rife"
RIFE_MODEL = "rife-v4.6"
FFMPEG = ROOT / "setup" / "ffmpeg"
FFMPEG_URL = "https://github.com/GyanD/codexffmpeg/releases/download/8.0.1/ffmpeg-8.0.1-essentials_build.zip"
FFMPEG_SHA256 = "e2aaeaa0fdbc397d4794828086424d4aaa2102cef1fb6874f6ffd29c0b88b673"
FFMPEG_MEMBERS = ("bin/ffmpeg.exe", "bin/ffprobe.exe", "LICENSE")
PIECE = 600  # pictures given to RIFE at once (about 1.5 GB of 1080p PNGs, and twice that made)
CRF = 16  # x264's quality: the 30 fps videos' look at about their size
FIRST = "hd802"  # the opening first: the one a player sees first
RIFE_TRIES = 3  # RIFE (the graphics driver under it) can crash once in a while: a piece is tried again
# SetThreadExecutionState: the computer kept from sleeping while this runs; the display still
# turns off when the player's settings say (with Modern Standby too, the computer then stays awake).
ES_CONTINUOUS, ES_SYSTEM_REQUIRED = 0x80000000, 0x00000001
AC_OFFLINE = 0  # SYSTEM_POWER_STATUS.ACLineStatus: on battery
MB_RETRYCANCEL, MB_ICONWARNING, IDRETRY = 0x5, 0x30, 4
ON_BATTERY = (
    "This computer is running on its battery. Making the HD videos at 60 fps takes hours, and on "
    "battery Windows may put the computer to sleep before they are done.\n\n"
    "Plug in the charger, then press Retry.\n"
    "Cancel makes them anyway: if the computer sleeps, run this again to go on from where it stopped."
)


class PowerStatus(ctypes.Structure):
    _fields_ = [
        ("ac_line", ctypes.c_ubyte),
        ("battery_flag", ctypes.c_ubyte),
        ("battery_percent", ctypes.c_ubyte),
        ("saver", ctypes.c_ubyte),
        ("life", ctypes.c_ulong),
        ("full_life", ctypes.c_ulong),
    ]


def say(text: str) -> None:
    print(text, flush=True)


def on_battery() -> bool:
    status = PowerStatus()
    return bool(ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status))) and status.ac_line == AC_OFFLINE


def stay_awake() -> None:
    """No sleep until this process ends (Windows gives the setting back when it does); the display
    turns off as usual. On battery Windows may sleep all the same: the player is asked to plug in."""
    if sys.platform != "win32":
        return
    ctypes.windll.kernel32.SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)
    while on_battery():
        answer = ctypes.windll.user32.MessageBoxW(
            None, ON_BATTERY, "Smooth the HD videos", MB_RETRYCANCEL | MB_ICONWARNING
        )
        if answer != IDRETRY:
            say("On battery: if the computer sleeps, run this again to go on.")
            break


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch_ffmpeg() -> None:
    """ffmpeg into FFMPEG, fetched once and checked."""
    if all((FFMPEG / Path(m).name).is_file() for m in FFMPEG_MEMBERS):
        return
    FFMPEG.mkdir(parents=True, exist_ok=True)
    archive = FFMPEG / "ffmpeg.zip"
    say(f"Fetching ffmpeg (about 100 MB) from {FFMPEG_URL} ...")
    try:
        urllib.request.urlretrieve(FFMPEG_URL, archive)
    except urllib.error.URLError as e:
        sys.exit(f"smooth_movies: ffmpeg could not be fetched ({e.reason}): check the connection and run again")
    got = sha256(archive)
    if got != FFMPEG_SHA256:
        archive.unlink()
        sys.exit(f"smooth_movies: the ffmpeg fetched has SHA-256 {got}, not {FFMPEG_SHA256}: not used")
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            for member in FFMPEG_MEMBERS:
                if info.filename.endswith("/" + member):
                    (FFMPEG / Path(member).name).write_bytes(z.read(info))
    archive.unlink()


def probe(ffprobe: Path, video: Path) -> tuple[int, Fraction, float]:
    """The video's pictures, their rate and its length in seconds."""
    out = subprocess.run(
        [
            str(ffprobe),
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-count_packets",
            "-show_entries",
            "stream=nb_read_packets,r_frame_rate:format=duration",
            "-of",
            "json",
            str(video),
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    j = json.loads(out)
    stream = j["streams"][0]
    return int(stream["nb_read_packets"]), Fraction(stream["r_frame_rate"]), float(j["format"]["duration"])


def made(ffprobe: Path, source: Path, target: Path) -> bool:
    """`target` is whole: twice `source`'s pictures."""
    if not target.is_file():
        return False
    try:
        return probe(ffprobe, target)[0] == 2 * probe(ffprobe, source)[0]
    except (subprocess.CalledProcessError, KeyError, IndexError, ValueError):
        return False


def clear(folder: Path) -> None:
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)


def smooth(tools: dict[str, Path], source: Path, target: Path, done_before: int, total: int, began: float) -> int:
    """`source` at twice its rate into `target`; returns its pictures (the source's)."""
    count, rate, _ = probe(tools["ffprobe"], source)
    part = target.with_name(target.stem + ".part.mp4")
    silent = WORK / "video.mp4"
    encoder = subprocess.Popen(
        [
            str(tools["ffmpeg"]),
            "-v",
            "error",
            "-y",
            "-f",
            "image2pipe",
            "-c:v",
            "png",
            "-framerate",
            str(rate * 2),
            "-i",
            "-",
            "-c:v",
            "libx264",
            "-crf",
            str(CRF),
            "-preset",
            "slow",
            "-profile:v",
            "high",
            "-level",
            "4.2",
            "-pix_fmt",
            "yuv420p",
            str(silent),
        ],
        stdin=subprocess.PIPE,
    )
    first = 0
    while first < count:
        last = min(first + PIECE, count - 1)  # the piece and the next one's first picture
        given = last - first + 1
        pieces_in, pieces_out = WORK / "in", WORK / "out"
        clear(pieces_in)
        clear(pieces_out)
        # accurate seeking: the first picture at or after half a picture before `first`
        start = float((Fraction(first) - Fraction(1, 2)) / rate) if first else 0.0
        subprocess.run(
            [
                str(tools["ffmpeg"]),
                "-v",
                "error",
                "-ss",
                f"{start:.6f}",
                "-i",
                str(source),
                "-frames:v",
                str(given),
                "-start_number",
                "0",
                str(pieces_in / "%06d.png"),
            ],
            check=True,
        )
        got = len(list(pieces_in.glob("*.png")))
        if got != given:
            sys.exit(f"smooth_movies: {source.name}: {got} pictures read at {first}, not {given}")
        # RIFE's picture i is at time i * given / (2 * given): every half picture
        for attempt in range(1, RIFE_TRIES + 1):
            clear(pieces_out)
            rife = subprocess.run(
                [
                    str(tools["rife"]),
                    "-i",
                    str(pieces_in),
                    "-o",
                    str(pieces_out),
                    "-m",
                    str(tools["model"].parent),
                    "-n",
                    str(2 * given),
                    "-f",
                    "%06d.png",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if rife.returncode == 0:
                break
            say(f"  {source.stem}: RIFE stopped at {first} (code {rife.returncode:#x}); try {attempt} of {RIFE_TRIES}")
        else:
            sys.exit(f"smooth_movies: {source.name}: RIFE failed {RIFE_TRIES} times at {first}: run it again to go on")
        made_now = sorted(pieces_out.glob("*.png"))
        if len(made_now) != 2 * given:
            sys.exit(f"smooth_movies: {source.name}: RIFE made {len(made_now)} pictures at {first}, not {2 * given}")
        # the next piece begins at `last`: its pictures from there are its own
        keep = made_now if last == count - 1 else made_now[: 2 * (given - 1)]
        for picture in keep:
            encoder.stdin.write(picture.read_bytes())
        first = last if last < count - 1 else count
        done = done_before + min(first, count)
        spent = time.monotonic() - began
        left = spent / done * (total - done) if done else 0
        say(
            f"  {source.stem}: {min(first, count)}/{count} pictures; all: {100 * done / total:.1f}%, "
            f"about {left / 3600:.1f} h left"
        )
    encoder.stdin.close()
    if encoder.wait() != 0:
        sys.exit(f"smooth_movies: {source.name}: the encoder failed")
    subprocess.run(
        [
            str(tools["ffmpeg"]),
            "-v",
            "error",
            "-y",
            "-i",
            str(silent),
            "-i",
            str(source),
            "-map",
            "0:v",
            "-map",
            "1:a?",
            "-c",
            "copy",
            "-movflags",
            "+faststart",
            str(part),
        ],
        check=True,
    )
    part.replace(target)
    return count


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", nargs="*", help="these videos' names only (hd802 ...)")
    ap.add_argument("--rife", type=Path, default=RIFE, help="rife-ncnn-vulkan's folder (with rife-v4.6)")
    ap.add_argument("--ffmpeg", type=Path, help="a folder with ffmpeg.exe and ffprobe.exe (else fetched)")
    ap.add_argument("--pause", action="store_true", help="wait for Enter before closing (its own window)")
    args = ap.parse_args()
    say("Making the HD videos at 60 fps. This takes hours; close this window at any time, and run it")
    say("again (Start menu: Smooth the HD videos) to go on from where it stopped.\n")
    if args.ffmpeg is None:
        fetch_ffmpeg()
        args.ffmpeg = FFMPEG
    tools = {
        "ffmpeg": args.ffmpeg / "ffmpeg.exe",
        "ffprobe": args.ffmpeg / "ffprobe.exe",
        "rife": args.rife / "rife-ncnn-vulkan.exe",
        "model": args.rife / RIFE_MODEL / "flownet.param",
    }
    for name, path in tools.items():
        if not path.is_file():
            sys.exit(f"smooth_movies: {name} is not at {path}")
    videos = sorted(SOURCE.glob("*.mp4"), key=lambda p: (p.stem != FIRST, p.stem))
    if args.only:
        videos = [v for v in videos if v.stem in args.only]
    if not videos:
        sys.exit(f"smooth_movies: no HD videos in {SOURCE}: make them with the installer's HD videos first")
    OUT.mkdir(parents=True, exist_ok=True)
    stay_awake()
    todo = [v for v in videos if not made(tools["ffprobe"], v, OUT / v.name)]
    say(f"{len(videos) - len(todo)} of {len(videos)} videos already at 60 fps; {len(todo)} to make.")
    if not todo:
        return 0
    total = sum(probe(tools["ffprobe"], v)[0] for v in todo)
    began, done = time.monotonic(), 0
    for i, video in enumerate(todo, 1):
        say(f"[{i}/{len(todo)}] {video.name}")
        done += smooth(tools, video, OUT / video.name, done, total, began)
    shutil.rmtree(WORK, ignore_errors=True)
    say(f"Done: {len(videos)} videos at 60 fps in {OUT}. In the game: Settings, Video, Movies: HD smooth.")
    return 0


if __name__ == "__main__":
    try:
        code = main()
    except subprocess.CalledProcessError as failed:
        print(
            f"smooth_movies: {Path(failed.cmd[0]).name} failed (code {failed.returncode:#x}): run it again to go on",
            file=sys.stderr,
            flush=True,
        )
        code = 1
    except SystemExit as stop:
        if isinstance(stop.code, str):
            print(stop.code, file=sys.stderr, flush=True)
            code = 1
        else:
            code = stop.code or 0
    if "--pause" in sys.argv:
        input("\nPress Enter to close this window.")
    sys.exit(code)
