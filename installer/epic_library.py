"""Where the Epic Games Store keeps the KINGDOM HEARTS collections on this computer: its launcher's
manifests (`<ProgramData>/Epic/EpicGamesLauncher/Data/Manifests/*.item`, JSON, each game's folder in
`InstallLocation`), and the collection's default folder (`C:/Program Files/Epic Games/KH_1.5_2.5`,
KHPCPatchManager's). A collection is the folder that holds its archives in `Image/en` (the Epic
Games Store's copies) or `Image/dt` (Steam's). Read only."""

from __future__ import annotations

import json
import os
from pathlib import Path

# the archive that tells each collection apart (installer/setup_steps.py COLLECTION_ARCHIVES)
MARKER_1525 = "kh2_first"
MARKER_28 = "kh3d_first"
LANGUAGE_FOLDERS = ("dt", "en")
DEFAULT_FOLDERS = (Path(r"C:\Program Files\Epic Games\KH_1.5_2.5"),)


def manifests_folder() -> Path:
    return Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData")) / "Epic" / "EpicGamesLauncher" / "Data" / "Manifests"


def install_folders(manifests: Path | None = None) -> list[Path]:
    """Every game folder the launcher's manifests name that is there, in the manifests' order."""
    folder = manifests if manifests is not None else manifests_folder()
    found: list[Path] = []
    try:
        items = sorted(folder.glob("*.item"))
    except OSError:
        return found
    for item in items:
        try:
            place = json.loads(item.read_text(encoding="utf-8", errors="replace")).get("InstallLocation")
        except (OSError, ValueError, AttributeError):
            continue  # a manifest the launcher is writing, or not a game's
        if isinstance(place, str) and place and Path(place).is_dir() and Path(place) not in found:
            found.append(Path(place))
    return found


def holds(folder: Path, marker: str) -> bool:
    """Whether `folder` holds the collection whose archive is `marker` (Image/<language>/<marker>.hed)."""
    return any((folder / "Image" / language / f"{marker}.hed").is_file() for language in LANGUAGE_FOLDERS)


def collection_folder(marker: str, manifests: Path | None = None, defaults=DEFAULT_FOLDERS) -> Path | None:
    """The Epic Games Store's folder of the collection whose archive is `marker`, or None."""
    for folder in [*install_folders(manifests), *defaults]:
        if holds(folder, marker):
            return folder
    return None
