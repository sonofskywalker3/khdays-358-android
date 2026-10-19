"""tools/hd/ds_scripts.py on synthetic bytes: P2 archives with and without names (an LZ10 entry among
them), a CAKP container's sections, a script's commands, its skip target and its messages (one with
a language absent), and what is not a script. No game data is read."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ds_scripts as ds  # noqa: E402


def lz10_literal(data: bytes) -> bytes:
    """An LZ10 stream of literals only (a flag byte of 0 before each 8 bytes)."""
    out = bytearray(b"\x10" + len(data).to_bytes(3, "little"))
    for at in range(0, len(data), 8):
        out.append(0)
        out += data[at : at + 8]
    return bytes(out)


def p2(entries: list[tuple[str | None, bytes, bool]]) -> bytes:
    """A P2 archive: (name or None, data, compressed?) each, entries at 0x200-byte sectors."""
    count = len(entries)
    named = entries and entries[0][0] is not None
    sizes_at = 0x10 + (count + 1) // 2 * 4
    header = bytearray(sizes_at + 4 * count + (8 * count if named else 0))
    header[:2] = b"P2"
    struct.pack_into("<H", header, 2, count | (0x8000 if named else 0))
    start = (len(header) + 0x1FF) & ~0x1FF
    struct.pack_into("<I", header, 0xC, start)
    body = bytearray()
    for i, (name, data, compressed) in enumerate(entries):
        stored = lz10_literal(data) if compressed else data
        struct.pack_into("<H", header, 0x10 + 2 * i, len(body) // 0x200)
        struct.pack_into("<I", header, sizes_at + 4 * i, len(stored) | (0x80000000 if compressed else 0))
        if named:
            header[sizes_at + 4 * count + 8 * i : sizes_at + 4 * count + 8 * i + len(name)] = name.encode()
        body += stored
        body += bytes((-len(body)) % 0x200)
    return bytes(header) + bytes(start - len(header)) + bytes(body)


def script() -> bytes:
    """A scene's script: its skip target (0.10, operand 0x1c), a speech step naming message 0, a step
    naming a value that is no record, the target cleared; then the pool and the records."""
    strings = [b"Up this way.", b"Par ici.", b"Hier lang.", b"Di qua.", b"Por aqui."]
    pool = bytearray()
    offsets = []
    for s in strings:
        offsets.append(len(pool))
        pool += s + b"\0"
        pool += bytes((-len(pool)) % 4)
    record_at = len(pool)
    pool += struct.pack("<7i", -1, *offsets, -1)  # jp absent, en fr de it es, then -1
    steps = bytearray()
    steps += bytes([0, 0x0A]) + struct.pack("<H", 2) + struct.pack("<i", 0x1C)
    steps += bytes([1, 0x1D]) + struct.pack("<H", 3) + struct.pack("<hhi", 0x40, 0, record_at)
    steps += bytes([1, 0x1D]) + struct.pack("<H", 3) + struct.pack("<hhi", 0x40, 0, 2)
    steps += bytes([0, 0x0A]) + struct.pack("<H", 2) + struct.pack("<i", -1)
    return struct.pack("<I", 4 + len(steps)) + bytes(steps) + bytes(pool)


def cakp(sections: dict[str, bytes]) -> bytes:
    """A CAKP container: the names block and the sections' table where ds.cakp_sections looks."""
    names = list(sections)
    count = len(names)
    first, second = 0x28, 0x34
    names_at = 0x34 + 4 + 8 * count
    name_offsets = [0]
    blob = bytearray()
    for name in names:
        name_offsets.append(2 * (count + 1) + len(blob))
        blob += name.encode() + b"\0"
    head = bytearray(names_at)
    head[:4] = b"CAKP"
    struct.pack_into("<II", head, 8, first, second)
    struct.pack_into("<II", head, first, 1, names_at)
    head += struct.pack(f"<{count + 1}H", *name_offsets) + blob
    head += bytes((-len(head)) % 4)
    offsets, sizes, data = [], [], bytearray()
    for name in names:
        offsets.append(len(head) + len(data))
        sizes.append(len(sections[name]))
        data += sections[name]
    struct.pack_into("<I", head, second, count)
    struct.pack_into(f"<{count}I", head, second + 4, *offsets)
    struct.pack_into(f"<{count}I", head, second + 4 + 4 * count, *sizes)
    return bytes(head) + bytes(data)


class P2Archives(unittest.TestCase):
    def test_unnamed_and_compressed(self):
        archive = p2([(None, b"first entry", False), (None, b"0123456789abcdef!", True)])
        self.assertEqual(ds.p2_entries(archive), [("0", b"first entry"), ("1", b"0123456789abcdef!")])

    def test_named(self):
        archive = p2([("11.Z", b"area", True), ("em0", b"enemies", False)])
        self.assertEqual(ds.p2_entries(archive), [("11.Z", b"area"), ("em0", b"enemies")])

    def test_not_p2(self):
        with self.assertRaises(ValueError):
            ds.p2_entries(b"NOPE" + bytes(32))


class Scripts(unittest.TestCase):
    def test_commands_and_census(self):
        s = script()
        self.assertEqual([(op, sub) for _, op, sub, _ in ds.commands(s)], [(0, 0x0A), (1, 0x1D), (1, 0x1D), (0, 0x0A)])
        items = ds.census_of(s)
        self.assertEqual(items[0], {"at": 4, "target": 0x1C})
        self.assertEqual(
            items[1]["line"],
            {"en": "Up this way.", "fr": "Par ici.", "de": "Hier lang.", "it": "Di qua.", "es": "Por aqui."},
        )
        self.assertEqual(items[2], {"at": 0x24, "target": -1})
        self.assertEqual(len(items), 3)  # the step naming no record names no line

    def test_not_a_script(self):
        self.assertIsNone(ds.census_of(b"\x01\x02"))
        self.assertIsNone(ds.census_of(struct.pack("<I", 0x9999) + bytes(8)))

    def test_cakp_sections(self):
        container = cakp({"_e": script(), "t_ax": b"\x08\x00\x00\x00" + bytes(4)})
        sections = ds.cakp_sections(container)
        self.assertEqual(list(sections), ["_e", "t_ax"])
        self.assertEqual(sections["_e"], script())
        with self.assertRaises(ValueError):
            ds.cakp_sections(b"KAPH" + bytes(64))


if __name__ == "__main__":
    unittest.main()
