"""tools/hd/theater_movies.py without the collection: the videos the scene map names, made from a
scrambled source and from a plain one, a video already there kept, the opening's and the ending's
names, and the map's stems read past its comments and `off` entries; the music: an SCD's Ogg and its
loop's sample, the event files' BGM schedules, and the schedules and tracks written for the videos."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import theater_movies as tm  # noqa: E402

PLAIN = b"\x00\x00\x00\x20ftypmp42" + bytes(range(256)) * 2  # an MP4's first bytes, then anything


def scrambled(data: bytes) -> bytes:
    out = bytearray(data)
    for i in range(min(tm.SCRAMBLED, len(out))):
        out[i] ^= tm.PATTERN[i % 16]
    return bytes(out)


class Movies(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.movies = root / "dt"
        self.movies.mkdir()
        self.saved = tm.OUT, tm.SCENE_MAP
        tm.OUT = root / "cinematics"
        tm.SCENE_MAP = root / "hd_scenes.txt"
        tm.SCENE_MAP.write_text(
            "# the port's\nEV_S#196 hd002 ; pictures\nEV_S#3 off ; none\nmnl#0 hd000 # a comment\n"
            "EV_S#169 hd131a hd132\n",
            encoding="utf-8",
        )

    def tearDown(self):
        tm.OUT, tm.SCENE_MAP = self.saved
        self.temp.cleanup()

    def test_scene_stems(self):
        self.assertEqual(tm.scene_stems(), ["hd002", "hd000", "hd131a", "hd132"])

    def test_videos(self):
        (self.movies / "hd002.mp4").write_bytes(scrambled(PLAIN))  # as the collection ships them
        (self.movies / "hd000.mp4").write_bytes(PLAIN)  # one shipped plain (the player's hd129)
        (self.movies / "hd818.mp4").write_bytes(scrambled(PLAIN))
        (self.movies / "DOP.mp4").write_bytes(scrambled(PLAIN))
        (self.movies / "hd131a.mp4").write_bytes(b"not a video at all, scrambled or not")
        made = tm.videos(self.movies, force=False)
        self.assertEqual(made, 4)  # hd002, hd000, hd818 and the opening; hd131a refused, hd132 absent
        for name in ("hd002.mp4", "hd000.mp4", "hd818.mp4", "hd802.mp4"):
            self.assertEqual((tm.OUT / name).read_bytes(), PLAIN, name)
        self.assertFalse((tm.OUT / "hd131a.mp4").exists())
        # kept unless forced
        (tm.OUT / "hd002.mp4").write_bytes(b"the player's own")
        self.assertEqual(tm.videos(self.movies, force=False), 0)
        self.assertEqual((tm.OUT / "hd002.mp4").read_bytes(), b"the player's own")
        self.assertEqual(tm.videos(self.movies, force=True), 4)
        self.assertEqual((tm.OUT / "hd002.mp4").read_bytes(), PLAIN)


def page(granule: int, body: bytes) -> bytes:
    """An Ogg page as far as the reader looks: its magic, its granule, its segments and its body."""
    segments = [255] * (len(body) // 255) + [len(body) % 255]
    return b"OggS\x00\x00" + struct.pack("<q", granule) + bytes(12) + bytes([len(segments)]) + bytes(segments) + body


KEY = 0x76
HEADER_PAGES = page(0, b"\x01vorbis" + bytes(30)) + page(0, b"\x03vorbis" + bytes(300))
STREAM = [page(1000, bytes(400)), page(2000, bytes(500)), page(3000, bytes(260))]


def scd(loop_page: int | None, codec: int = tm.OGG) -> bytes:
    """An SCD of one sound: its tables at 0x30, its sound's header at 0x60, then the Ogg header,
    a seek table, the Vorbis header pages XORed with KEY and the stream; looping from the start of
    STREAM's page `loop_page` (None: it plays once)."""
    stream = b"".join(STREAM)
    seek = bytes(16)
    vorbis = bytes(b ^ KEY for b in HEADER_PAGES)
    loop_start = len(b"".join(STREAM[:loop_page])) if loop_page is not None else 0
    loop_end = len(stream) if loop_page is not None else 0
    extra = 0x20 + len(seek) + len(vorbis)
    out = bytearray(b"SEDBSSCF" + struct.pack("<I", 3) + b"\x00\x04" + struct.pack("<H", 0x30) + bytes(0x20))
    out += struct.pack("<4H4I", 1, 1, 1, 0, 0x50, 0x58, 0x50, 0) + bytes(0x50 - 0x48)  # tables' header
    out += struct.pack("<4I", 0, 0, 0x60, 0)  # 0x50: the first table's; 0x58: the sounds' offsets
    out += struct.pack("<8i", len(stream), 2, 48000, codec, loop_start, loop_end, extra, 0)
    out += bytes([2, 0x20, KEY, 0]) + bytes(12) + struct.pack("<II", len(seek), len(vorbis)) + bytes(8)
    return bytes(out + seek + vorbis + stream)


EVENT = """<?xml version="1.0"?>
<EVENT>
<SCHEDULE_CATEGORY type = "SCHEDULE_CATEGORY_TEXT_MOVIE">
<PARAM ScheduleID="0"/>
</SCHEDULE_CATEGORY>
<SCHEDULE_CATEGORY type = "SCHEDULE_CATEGORY_BGM">
    <PARAM ScheduleID="1" EnableBgm1="True" BgmStartSet="FIELD_BATTLE" BgmEndSet="STOP" EndBgmVolume="100"/>
    <SCHEDULE_BGM>
        <BGM1>
            <DATA FrameStart="390" FrameEnd="5080" SoundName="bgm_222" Track="TRACK_FIELD" />
        </BGM1>
        <VOLUME1>
            <DATA FrameStart="390" FrameEnd="390" GroupID="0" CurveID="0" Param="50" />
            <DATA FrameStart="5000" FrameEnd="5080" GroupID="0" CurveID="0" Param="0" />
        </VOLUME1>
    </SCHEDULE_BGM>
</SCHEDULE_CATEGORY>
</EVENT>
"""


class Music(unittest.TestCase):
    def test_scd_ogg(self):
        ogg, loop, rate = tm.scd_ogg(scd(1))
        self.assertEqual(ogg, HEADER_PAGES + b"".join(STREAM))  # the header pages unXORed, then the stream
        self.assertEqual((loop, rate), (1000, 48000))  # the granule of the page before the loop's
        self.assertEqual(tm.scd_ogg(scd(0))[1], 0)
        self.assertEqual(tm.scd_ogg(scd(None))[1], -1)  # its loop's end 0: it plays once
        with self.assertRaises(ValueError):
            tm.scd_ogg(scd(1, codec=0x0C))
        with self.assertRaises(ValueError):
            tm.scd_ogg(b"RIFF" + bytes(60))

    def test_bgm_schedule(self):
        plan = tm.bgm_schedule(EVENT)
        self.assertEqual(plan["from"], "field")  # FIELD_BATTLE is the field's too
        self.assertEqual(plan["to"], "stop")
        self.assertEqual(plan["cues"], [(390, 5080, "bgm_222")])
        self.assertEqual(plan["volume"], [(390, 390, 50), (5000, 5080, 0)])
        self.assertIsNone(tm.bgm_schedule(EVENT.replace("SCHEDULE_CATEGORY_BGM", "SCHEDULE_CATEGORY_SOUND")))
        no_cue = EVENT.replace('<DATA FrameStart="390" FrameEnd="5080" SoundName="bgm_222" Track="TRACK_FIELD" />', "")
        self.assertIsNone(tm.bgm_schedule(no_cue))

    def test_music(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            mare = root / "original"
            for stem in ("hd848", "hd002", "hd055"):
                (mare / "uk" / "event" / "en" / "hd" / stem).mkdir(parents=True)
            (mare / "uk" / "event" / "en" / "hd" / "hd848" / "hd848.exia2").write_text(EVENT, encoding="utf-8")
            (mare / "uk" / "event" / "en" / "hd" / "hd002" / "hd002.exia2").write_text("<EVENT/>", encoding="utf-8")
            (mare / "uk" / "event" / "en" / "hd" / "hd055" / "hd055.exia2").write_text(EVENT, encoding="utf-8")
            (mare / "sound").mkdir()
            (mare / "sound" / "bgm_222.win32.scd").write_bytes(scd(1))
            saved = tm.OUT, tm.SCENE_MAP
            tm.OUT, tm.SCENE_MAP = root / "cinematics", root / "hd_scenes.txt"
            tm.SCENE_MAP.write_text("EV_S#196 hd002\n", encoding="utf-8")  # hd055: not a video the port plays
            try:
                self.assertEqual(tm.music(mare), 1)
            finally:
                out, tm.OUT, tm.SCENE_MAP = tm.OUT / "music", *saved
            lines = [line for line in (out / "hd848.txt").read_text(encoding="utf-8").splitlines() if line[:1] != "#"]
            self.assertEqual(
                lines,
                [
                    "rate 30",
                    "from field",
                    "to stop",
                    "cue 390 5080 bgm_222 1000",
                    "volume 390 390 50",
                    "volume 5000 5080 0",
                ],
            )
            self.assertEqual((out / "bgm_222.ogg").read_bytes(), HEADER_PAGES + b"".join(STREAM))
            self.assertFalse((out / "hd002.txt").exists())  # no music of its own
            self.assertFalse((out / "hd055.txt").exists())


if __name__ == "__main__":
    unittest.main()
