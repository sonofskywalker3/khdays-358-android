from __future__ import annotations

import hashlib
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).resolve().parents[1] / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


verify_rom = load("verify_rom")
extract_data = load("extract_data")


class WholeDumpTests(unittest.TestCase):
    """A trimmed dump hashes as the whole dump it was cut from; any other file as it is."""

    def test_trimmed_dump(self) -> None:
        used = 0x400
        rom = bytearray(used)
        rom[0x0C:0x10] = b"TSTP"
        rom[0x14] = 0  # a 128 KiB cartridge
        rom[0x80:0x84] = used.to_bytes(4, "little")
        whole = bytes(rom) + b"\xff" * (0x20000 - used)
        with tempfile.TemporaryDirectory() as temporary:
            trimmed_path = Path(temporary) / "trimmed.nds"
            whole_path = Path(temporary) / "whole.nds"
            trimmed_path.write_bytes(rom)
            whole_path.write_bytes(whole)
            expected = hashlib.sha256(whole).hexdigest()
            for tool in (verify_rom, extract_data):
                self.assertEqual(tool.whole_dump_sha256(trimmed_path), expected)
                self.assertEqual(tool.whole_dump_sha256(whole_path), expected)
            self.assertEqual(verify_rom.read_rom_info(trimmed_path).sha256, expected)

    def test_other_files_as_they_are(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "short.nds"
            data = bytes(0x300)  # no used size in its header: not a trimmed dump
            path.write_bytes(data)
            for tool in (verify_rom, extract_data):
                self.assertEqual(tool.whole_dump_sha256(path), hashlib.sha256(data).hexdigest())


class HeaderFreeTests(unittest.TestCase):
    """Two dumps of one cartridge that differ in the header's area 0x200-0xFFF (which nothing loads) are
    the same ROM: the second hash leaves that area out, and the supported entry's matches either."""

    def test_dumpers_header_area(self) -> None:
        rom = bytearray(0x20000)
        rom[0x0C:0x10] = b"TSTP"
        rom[0x14] = 0  # a 128 KiB cartridge, whole
        rom[0x4000:0x4010] = b"the game's data!"
        other = bytearray(rom)
        rom[0x33C:0x350] = bytes(range(1, 21))  # what one dumper keeps there; the other zeroes
        with tempfile.TemporaryDirectory() as temporary:
            kept, zeroed = Path(temporary) / "kept.nds", Path(temporary) / "zeroed.nds"
            kept.write_bytes(rom)
            zeroed.write_bytes(other)
            entry = {
                "name": "test",
                "sha256": hashlib.sha256(rom).hexdigest(),
                "sha256_header_free": hashlib.sha256(other).hexdigest(),
            }
            for tool in (verify_rom, extract_data):
                self.assertNotEqual(tool.whole_dump_sha256(kept), tool.whole_dump_sha256(zeroed))
                self.assertEqual(
                    tool.whole_dump_sha256(kept, header_free=True), tool.whole_dump_sha256(zeroed, header_free=True)
                )
            for path in (kept, zeroed):
                self.assertIs(verify_rom.find_supported_rom(verify_rom.read_rom_info(path), [entry]), entry)
                self.assertIs(
                    extract_data.find_supported_rom(
                        extract_data.whole_dump_sha256(path),
                        [entry],
                        extract_data.whole_dump_sha256(path, header_free=True),
                    ),
                    entry,
                )
            # the game's own bytes are never left out
            other[0x4000] ^= 1
            zeroed.write_bytes(other)
            self.assertIsNone(verify_rom.find_supported_rom(verify_rom.read_rom_info(zeroed), [entry]))


if __name__ == "__main__":
    unittest.main()
