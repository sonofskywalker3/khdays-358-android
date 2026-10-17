#!/usr/bin/env python3
"""Build the Android app (arm64): prepare.py into build/port/gen-arm64, then android/'s Gradle build, which
compiles port/ with the NDK. The APK lands in android/build/outputs/apk/<variant>/.
  python port/tools/build_android.py [--release] [--skip-prepare] [--install]
prepare.py runs MSVC's preprocessor, so this runs on Windows, as port/build.py does.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / "build" / "port" / "gen-arm64"
ANDROID = ROOT / "android"
PACKAGE = "com.sonofskywalker3.khdays358"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", action="store_true", help="the release variant (default: debug, optimised)")
    ap.add_argument("--skip-prepare", action="store_true", help="build from the gen-arm64 already made")
    ap.add_argument("--install", action="store_true", help="install the APK on the device adb sees")
    args = ap.parse_args()

    if not args.skip_prepare:
        subprocess.run([sys.executable, str(ROOT / "port" / "tools" / "prepare.py"), "--out", str(GEN)], check=True)
    elif not (GEN / "sources.cmake").exists():
        raise SystemExit(f"no prepared sources in {GEN}: run without --skip-prepare")

    variant = "Release" if args.release else "Debug"
    gradlew = ANDROID / ("gradlew.bat" if os.name == "nt" else "gradlew")
    subprocess.run([str(gradlew), f"assemble{variant}"], cwd=ANDROID, check=True)
    apk_dir = ANDROID / "build" / "outputs" / "apk" / variant.lower()
    apks = sorted(apk_dir.glob("*.apk"))
    if not apks:
        raise SystemExit(f"no APK in {apk_dir}")
    print(f"APK: {apks[-1]}")

    if args.install:
        adb = shutil.which("adb")
        if adb is None:
            sdk = os.environ.get("ANDROID_HOME") or str(Path.home() / "AppData" / "Local" / "Android" / "Sdk")
            adb = str(Path(sdk) / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb"))
        subprocess.run([adb, "install", "-r", str(apks[-1])], check=True)
        print(f"installed {PACKAGE}; the game's folder is /sdcard/Android/data/{PACKAGE}/files (data/, saves/)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
