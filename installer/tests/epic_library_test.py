"""The Epic Games Store's collections (epic_library.py) and the collections' places in either store's
layout (setup_steps.py archive_base, movies_folder) on synthetic folders: the launcher's manifests read
(a broken one and a game elsewhere skipped), a collection told by its archive in Image/en or Image/dt,
and the 1.5+2.5's archives and videos found in an Epic-like tree as in a Steam one. No real store is
read (the collections' own files are never in the repository)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import epic_library as el  # noqa: E402
import setup_steps as s  # noqa: E402


def touch(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")


class EpicLibrary(unittest.TestCase):
    def test_manifests_and_collections(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t)
            manifests = root / "Manifests"
            manifests.mkdir()
            kh = root / "Games" / "KH_1.5_2.5"
            dl = root / "Games" / "KH_2.8"
            other = root / "Games" / "Another"
            for folder in (kh, dl, other):
                folder.mkdir(parents=True)
            touch(kh / "Image" / "en" / "kh2_first.hed")
            touch(dl / "Image" / "dt" / "kh3d_first.hed")
            for name, place in (("a", other), ("b", kh), ("c", dl), ("d", root / "gone")):
                (manifests / f"{name}.item").write_text(json.dumps({"InstallLocation": str(place)}), encoding="utf-8")
            (manifests / "e.item").write_text("{not json", encoding="utf-8")
            self.assertEqual(el.install_folders(manifests), [other, kh, dl])
            self.assertEqual(el.collection_folder(el.MARKER_1525, manifests, ()), kh)
            self.assertEqual(el.collection_folder(el.MARKER_28, manifests, ()), dl)
            self.assertIsNone(el.collection_folder("kh9_first", manifests, ()))
            self.assertEqual(el.install_folders(root / "none"), [])


class Places(unittest.TestCase):
    def test_epic_and_steam_layouts(self):
        movies = s.hd_inputs().get("movies", [])
        self.assertTrue(movies)
        with tempfile.TemporaryDirectory() as t:
            epic, steam = Path(t) / "epic", Path(t) / "steam"
            touch(epic / "Image" / "en" / "kh2_first.hed")
            touch(steam / "Image" / "dt" / "kh2_first.hed")
            self.assertEqual(s.archive_base(epic, "1.5+2.5", "kh2_first"), epic / "Image" / "en" / "kh2_first")
            self.assertEqual(s.archive_base(steam, "1.5+2.5", "kh2_first"), steam / "Image" / "dt" / "kh2_first")
            self.assertEqual(s.archive_base(epic, "1.5+2.5", "Mare"), epic / "Image" / "Mare")
            # the videos: Steam's place, else the one like it that holds them
            touch(steam / s.MOVIES / movies[0])
            touch(epic / "EPIC" / "Mare" / "MOVIE" / "Days" / "en" / movies[0])
            touch(epic / "EPIC" / "Mare" / "MOVIE" / "Days" / "aa" / "other.mp4")
            self.assertEqual(s.movies_folder(steam), steam / s.MOVIES)
            self.assertEqual(s.movies_folder(epic), epic / "EPIC" / "Mare" / "MOVIE" / "Days" / "en")
            self.assertEqual(s.movies_folder(Path(t) / "none"), Path(t) / "none" / s.MOVIES)


if __name__ == "__main__":
    unittest.main()
