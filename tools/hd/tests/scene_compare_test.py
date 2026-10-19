"""tools/hd/scene_compare.py without the game or a video: the map's entries that play videos, the
game's pictures picked at equal fractions without the black ones, and a sheet's size."""

import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import scene_compare as sc  # noqa: E402


class Compare(unittest.TestCase):
    def test_map_entries(self):
        with tempfile.TemporaryDirectory() as t:
            path = Path(t) / "map.txt"
            path.write_text(
                "# a comment\nEV_S#196 hd002 ; pictures\nEV_S#3 off ; none\nEV_S#169 hd131a hd132 # split\n",
                encoding="utf-8",
            )
            self.assertEqual(
                sc.map_entries(path), [("EV_S#196", ["hd002"], "pictures"), ("EV_S#169", ["hd131a", "hd132"], "")]
            )

    def test_picked_leaves_black_out(self):
        black = [Image.new("RGB", (4, 4)) for _ in range(5)]
        lit = [Image.new("RGB", (4, 4), (100 + i, 100, 100)) for i in range(10)]
        picks = sc.picked(black + lit)
        self.assertEqual(len(picks), sc.COLUMNS)
        self.assertTrue(all(p.getpixel((0, 0))[0] >= 100 for p in picks))
        self.assertEqual(sc.picked([]), [])
        self.assertEqual(len(sc.picked(black)), sc.COLUMNS)  # all black: taken as they are

    def test_sheet(self):
        picture = Image.new("RGB", (1280, 720), (10, 20, 30))
        out = sc.sheet("EV_S#4", ["hd004"], "lines", [Image.new("RGB", (256, 192))] * 8, [picture] * 8)
        self.assertEqual(out.size, (sc.COLUMNS * sc.CELL_W, sc.CAPTION + 2 * sc.CELL_H))

    def test_ds_pictures(self):
        with tempfile.TemporaryDirectory() as t:
            shots = Path(t)
            for name in ("EV_S_4_02430.bmp", "EV_S_4_02400.bmp", "EV_S_40_02400.bmp", "EV_S_4_note.bmp"):
                (shots / name).write_bytes(b"")
            self.assertEqual(
                [p.name for p in sc.ds_pictures(shots, "EV_S#4")], ["EV_S_4_02400.bmp", "EV_S_4_02430.bmp"]
            )


if __name__ == "__main__":
    unittest.main()
