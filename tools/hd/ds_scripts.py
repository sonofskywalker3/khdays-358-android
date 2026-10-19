"""Days's event scripts read from the game's data: the event archives'
entries (ev/EV_*.p2) and the missions' script containers (mi/mi/<mission>, "CAKP"), each script's
commands, its skip targets and the messages its commands name.

A script: a u32 at 0, the offset of its string pool; its commands from 4 (u8 opcode, u8 sub-op, u16
the length in words in bits 0-10, the operands after it, 8 bytes each: an s16 type, then the value);
then the pool's UTF-8 strings and the messages' records (7 words: the offsets from the pool of the
Japanese, English, French, German, Italian and Spanish strings, -1 for one absent, then -1). An
operand of type 0x40 names a message by its record's offset from the pool. Opcode 0 sub-op 10 is
Game_ActionSetBranchTarget: its operand sets the skip target (-1 clears it).

    python tools/hd/ds_scripts.py <nitrofs> <out.json>   # every script's targets and lines
"""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import decompress_data as dd  # noqa: E402

LANGUAGES = ("jp", "en", "fr", "de", "it", "es")
MESSAGE = 0x40  # an operand naming a message
SKIP_TARGET = (0, 0x0A)  # Game_ActionSetBranchTarget


def p2_entries(data: bytes) -> list[tuple[str, bytes]]:
    """A P2 archive's entries, decompressed: (name, data); unnamed entries are named by index. The
    count's top bit marks names (8 bytes each after the sizes)."""
    if data[:2] != b"P2":
        raise ValueError("not a P2 archive")
    word = struct.unpack_from("<H", data, 2)[0]
    count = word & 0x7FFF
    start = struct.unpack_from("<I", data, 0xC)[0]
    offsets = struct.unpack_from(f"<{count}H", data, 0x10)
    sizes_at = 0x10 + (count + 1) // 2 * 4
    sizes = struct.unpack_from(f"<{count}I", data, sizes_at)
    names_at = sizes_at + 4 * count
    out = []
    for i in range(count):
        name = str(i)
        if word & 0x8000:
            name = data[names_at + 8 * i : names_at + 8 * i + 8].split(b"\0")[0].decode("ascii", "replace")
        at, size = offsets[i] * 0x200 + start, sizes[i] & 0x7FFFFFFF
        entry = data[at : at + size]
        if sizes[i] & 0x80000000:
            entry = dd.decompress_nitro(entry)[1]
        out.append((name, entry))
    return out


def cakp_sections(data: bytes) -> dict[str, bytes]:
    """A mission's script container: its named sections. The u32s at 8 and 12 give the names
    block's place (its offset at +4 of the first) and the sections' count, offsets and sizes."""
    if data[:4] != b"CAKP":
        raise ValueError("not a CAKP container")
    first, second = struct.unpack_from("<II", data, 8)
    names_at = struct.unpack_from("<I", data, first + 4)[0]
    count = struct.unpack_from("<I", data, second)[0]
    offsets = struct.unpack_from(f"<{count}I", data, second + 4)
    sizes = struct.unpack_from(f"<{count}I", data, second + 4 + 4 * count)
    name_offsets = struct.unpack_from(f"<{count + 1}H", data, names_at)
    out = {}
    for i in range(count):
        at = names_at + name_offsets[i + 1]
        name = data[at : data.index(b"\0", at)].decode("latin-1")
        out[name] = data[offsets[i] : offsets[i] + sizes[i]]
    return out


def commands(script: bytes) -> list[tuple[int, int, int, bytes]]:
    """(offset, opcode, sub-op, operand bytes) up to the string pool."""
    pool = struct.unpack_from("<I", script, 0)[0]
    out, at = [], 4
    while at + 4 <= pool:
        op, sub, word = script[at], script[at + 1], struct.unpack_from("<H", script, at + 2)[0]
        words = word & 0x7FF
        if words == 0:
            break
        out.append((at, op, sub, script[at + 4 : at + 4 * words]))
        at += 4 * words
    return out


def strings(script: bytes) -> dict[int, str]:
    """The pool's strings by their offset from the pool (4-aligned, NUL-ended)."""
    pool = struct.unpack_from("<I", script, 0)[0]
    out, at = {}, pool
    while at < len(script):
        if script[at] == 0:
            at += 1
            continue
        end = script.find(b"\0", at)
        if end < 0:
            break
        try:
            out[at - pool] = script[at:end].decode("utf-8")
        except UnicodeDecodeError:
            break
        at = (end + 4) & ~3
    return out


def message(script: bytes, value: int, pool_strings: dict[int, str]) -> dict[str, str] | None:
    """The message whose record is at `value` from the pool, or None when no record is there."""
    pool = struct.unpack_from("<I", script, 0)[0]
    at = pool + value
    if value < 0 or at + 28 > len(script) or at % 4:
        return None
    record = struct.unpack_from("<7i", script, at)
    if record[6] != -1 or record[1] not in pool_strings or record[5] not in pool_strings:
        return None
    if any(v != -1 and v not in pool_strings for v in record[:6]):
        return None
    return {lang: pool_strings[v] for lang, v in zip(LANGUAGES, record[:6]) if v != -1}


def census_of(script: bytes) -> list[dict] | None:
    """The script's skip targets and the messages its commands name, in command order; None when
    the bytes are not a script."""
    if len(script) < 8:
        return None
    pool = struct.unpack_from("<I", script, 0)[0]
    if not 4 < pool <= len(script):
        return None
    pool_strings = strings(script)
    out = []
    for at, op, sub, operands in commands(script):
        if (op, sub) == SKIP_TARGET and len(operands) >= 4:
            out.append({"at": at, "target": struct.unpack_from("<i", operands, 0)[0]})
            continue
        for k in range(0, len(operands) - 7, 8):
            if struct.unpack_from("<h", operands, k)[0] == MESSAGE:
                text = message(script, struct.unpack_from("<i", operands, k + 4)[0], pool_strings)
                if text is not None:
                    out.append({"at": at, "op": f"{op}.{sub}", "line": text})
    return out


def census(nitrofs: Path) -> dict[str, list[dict]]:
    """Every event archive entry (`EV_S#4`) and mission script section (`mi/10000/7:_e`)."""
    out = {}
    for archive in sorted((nitrofs / "ev").glob("EV_*.p2")):
        for name, data in p2_entries(archive.read_bytes()):
            items = census_of(data)
            if items is not None:
                out[f"{archive.stem}#{name}"] = items
    for path in sorted((nitrofs / "mi" / "mi").iterdir()):
        data = path.read_bytes()
        if data[:2] != b"P2":
            continue
        for name, entry in p2_entries(data):
            if entry[:4] != b"CAKP":
                continue
            try:
                sections = cakp_sections(entry)
            except (ValueError, struct.error):
                continue  # a container of another kind (the "mo" entries)
            for section, script in sections.items():
                items = census_of(script)
                if items:
                    out[f"mi/{path.name}/{name.removesuffix('.Z')}:{section}"] = items
    return out


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    result = census(Path(sys.argv[1]))
    Path(sys.argv[2]).write_text(json.dumps(result, ensure_ascii=False, indent=0), encoding="utf-8")
    targets = sum(1 for v in result.values() if any("target" in i for i in v))
    lines = sum(1 for v in result.values() if any("line" in i for i in v))
    print(f"{len(result)} scripts: {targets} with skip targets, {lines} with lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
