"""tools/hd/theater_data.py on synthetic files: the theater's scenes and chapters (THD, CHP) and its
story order, and a video's bytes scrambled, plain, or neither. No collection is read."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import theater_data as td  # noqa: E402
import theater_movies as tm  # noqa: E402


def thd(records: list[tuple[str, str, str]]) -> bytes:
    """THD@: 28-byte records from 0x10 (u16 4, 6, 8: the event's, video's and music's offsets in the
    strings after the records)."""
    strings = bytearray(b"\0")
    rows = []
    for event, video, music in records:
        offs = []
        for s in (event, video, music):
            if s:
                offs.append(len(strings))
                strings += s.encode() + b"\0"
            else:
                offs.append(0)
        row = [0] * 14
        row[4], row[6], row[8] = offs
        rows.append(struct.pack("<14H", *row))
    head = b"THD@" + struct.pack("<HH", 0, len(records)) + bytes(8)
    return head + b"".join(rows) + bytes(strings)


def chp(chapters: list[tuple[int, str, list[int]]]) -> bytes:
    """CHP@: 96-byte records from 8 (u16 1 the day, 4 the artwork's name's offset, 16 the count, 18 on
    the scenes)."""
    strings = bytearray(b"\0")
    rows = []
    for day, art, scenes in chapters:
        row = [0] * 48
        row[1] = day
        row[4] = len(strings)
        strings += art.encode() + b"\0"
        row[16] = len(scenes)
        row[18 : 18 + len(scenes)] = scenes
        rows.append(struct.pack("<48H", *row))
    return b"CHP@" + struct.pack("<HH", 0, len(chapters)) + b"".join(rows) + bytes(strings)


class Theater(unittest.TestCase):
    def test_scenes_chapters_story(self):
        with tempfile.TemporaryDirectory() as t:
            mare = Path(t)
            (mare / "data").mkdir()
            (mare / "data" / "DaysTheater.thd").write_bytes(
                thd([("hd802/hd802.exia2", "DOP.mp4", ""), ("", "", "bgm_203"), ("hd004/hd004.exia2", "hd004.mp4", "")])
            )
            (mare / "data" / "DaysTheater.chp").write_bytes(chp([(0, "tz000a.tm2", [0]), (8, "tz008b.tm2", [1, 2])]))
            scenes = td.scenes(mare)
            self.assertEqual(scenes[0], {"event": "hd802/hd802.exia2", "video": "DOP.mp4", "music": ""})
            self.assertEqual(scenes[1]["music"], "bgm_203")
            self.assertEqual([c["day"] for c in td.chapters(mare)], [0, 8])
            self.assertEqual(
                td.story(mare),
                [
                    {"chapter": 0, "day": 0, "scene": 0, "stem": "hd802", "video": "DOP.mp4"},
                    {"chapter": 1, "day": 8, "scene": 2, "stem": "hd004", "video": "hd004.mp4"},
                ],
            )

    def test_video_bytes(self):
        plain = bytes(4) + b"ftypmp42" + bytes(500)
        scrambled = bytearray(plain)
        for i in range(tm.SCRAMBLED):
            scrambled[i] ^= tm.PATTERN[i % 16]
        with tempfile.TemporaryDirectory() as t:
            a, b, c = Path(t) / "a.mp4", Path(t) / "b.mp4", Path(t) / "c.mp4"
            a.write_bytes(bytes(scrambled))
            b.write_bytes(plain)
            c.write_bytes(b"not a video at all" + bytes(300))
            self.assertEqual(td.video_bytes(a), plain)
            self.assertEqual(td.video_bytes(b), plain)
            with self.assertRaises(ValueError):
                td.video_bytes(c)


if __name__ == "__main__":
    unittest.main()
