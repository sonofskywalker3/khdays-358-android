"""tools/hd/scene_report.py without the game or the collection: where each video plays, and what the
map's checks catch."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import scene_report as sr  # noqa: E402


class Report(unittest.TestCase):
    ENTRIES = [
        ("EV_S#196", ["hd002"], "pictures"),
        ("EV_S#169", ["hd131a", "hd132"], ""),
        ("mnl#0", ["hd000"], "words"),
    ]

    def test_movie_of(self):
        self.assertEqual(sr.movie_of("hd818"), "818")
        self.assertEqual(sr.movie_of("DOP"), "802")
        self.assertEqual(sr.movie_of("DED"), "843")
        self.assertIsNone(sr.movie_of("hd002"))
        self.assertIsNone(sr.movie_of("hd131a"))

    def test_coverage(self):
        videos = ["CED", "DOP", "hd000", "hd002", "hd131a", "hd132", "hd140", "hd818", "hd844"]
        where = sr.coverage(videos, self.ENTRIES, {"802", "818"})
        self.assertEqual(where["hd002"], "scene EV_S#196")
        self.assertEqual(where["hd131a"], "scene EV_S#169")
        self.assertEqual(where["hd000"], "monologue mnl#0")
        self.assertEqual(where["DOP"], "movie 802")
        self.assertEqual(where["hd818"], "movie 818")
        self.assertTrue(where["hd844"].startswith("not played: the game has no movie 844"))
        self.assertTrue(where["CED"].startswith("not played"))
        self.assertTrue(where["hd140"].startswith("not played"))

    def test_check(self):
        videos = {"hd000", "hd002", "hd131a", "hd132"}
        self.assertEqual(sr.check(self.ENTRIES, videos, {"EV_S#196", "EV_S#169"}, 8), [])
        errors = sr.check(self.ENTRIES, videos - {"hd132"}, {"EV_S#196"}, 0)
        self.assertEqual(len(errors), 3)  # EV_S#169 not a script, hd132 missing, mnl#0 out of range
        self.assertTrue(any("EV_S#169: not a script" in e for e in errors))
        self.assertTrue(any("hd132 is not one" in e for e in errors))
        self.assertTrue(any("mnl#0: the game has 0" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
