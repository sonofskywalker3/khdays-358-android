"""The DS layout of the game's types for the data area, as the rewritten C compiles them: pointers 4
bytes, members aligned to at most 4 (/Zp4), bit-fields as MSVC packs them. The emitted C checks every
size it relies on with _Static_assert, so a wrong rule stops the build instead of misplacing an object.
"""

from __future__ import annotations

from pycparser import c_ast

from .types import Array, Arithmetic, CType, Enumeration, Function, Pointer, Record, TypeFailure, TypePass, Void

PACK = 4  # /Zp4

SCALARS = {
    "_Bool": 1,
    "char": 1,
    "signed char": 1,
    "unsigned char": 1,
    "short": 2,
    "unsigned short": 2,
    "int": 4,
    "unsigned int": 4,
    "long": 4,
    "unsigned long": 4,
    "float": 4,
    "long long": 8,
    "unsigned long long": 8,
    "double": 8,
    "long double": 8,
}


class Layout:
    def __init__(self, tp: TypePass):
        self.tp = tp

    # --- constants ---

    def value(self, node) -> int:
        """An integer constant expression's value."""
        if isinstance(node, c_ast.Constant):
            if node.type == "char":
                return char_value(node.value)
            text = node.value.rstrip("uUlL")
            if node.type in ("float", "double"):
                raise TypeFailure(node, "a floating constant where an integer is needed")
            return int(text, 8) if text.startswith("0") and len(text) > 1 and text[1] not in "xX" else int(text, 0)
        if isinstance(node, c_ast.ID):
            s = self.tp.symbols.get(id(node))
            if s is None or s.kind != "enumerator":
                raise TypeFailure(node, f"{node.name} is not a constant")
            return self.enumerator(s)
        if isinstance(node, c_ast.UnaryOp):
            if node.op == "sizeof":
                t = (
                    self.tp.type_of_declarator(node.expr)
                    if isinstance(node.expr, c_ast.Typename)
                    else self.tp.types[id(node.expr)]
                )
                return self.size(t)
            v = self.value(node.expr)
            return {"-": -v, "+": v, "~": ~v, "!": int(not v)}[node.op]
        if isinstance(node, c_ast.BinaryOp):
            a, b = self.value(node.left), self.value(node.right)
            ops = {
                "+": a + b,
                "-": a - b,
                "*": a * b,
                "<<": a << b,
                ">>": a >> b,
                "&": a & b,
                "|": a | b,
                "^": a ^ b,
                "<": int(a < b),
                ">": int(a > b),
                "<=": int(a <= b),
                ">=": int(a >= b),
                "==": int(a == b),
                "!=": int(a != b),
                "&&": int(bool(a and b)),
                "||": int(bool(a or b)),
            }
            if node.op in ("/", "%"):
                if b == 0:
                    raise TypeFailure(node, "a division by zero in a constant")
                q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
                return q if node.op == "/" else a - q * b
            return ops[node.op]
        if isinstance(node, c_ast.TernaryOp):
            return self.value(node.iftrue) if self.value(node.cond) else self.value(node.iffalse)
        if isinstance(node, c_ast.Cast):
            return self.value(node.expr)
        raise TypeFailure(node, f"a constant not evaluated: {type(node).__name__}")

    def enumerator(self, s) -> int:
        if s.node is not None and s.node.value is not None:
            return self.value(s.node.value)
        return 0 if s.previous is None else self.enumerator(s.previous) + 1

    # --- sizes ---

    def size(self, t: CType) -> int:
        return self.measure(t)[0]

    def align(self, t: CType) -> int:
        return self.measure(t)[1]

    def measure(self, t: CType) -> tuple[int, int]:
        """(size, alignment as a struct member)."""
        if isinstance(t, Arithmetic):
            n = SCALARS[t.spelling]
            return n, min(n, PACK)
        if isinstance(t, (Pointer, Enumeration)):
            return 4, 4
        if isinstance(t, Array):
            if t.length is None:
                raise TypeFailure(None, "the size of an array of unknown length")
            n, a = self.measure(t.element)
            return n * self.value(t.length), a
        if isinstance(t, Record):
            return self.record(t)
        if isinstance(t, (Void, Function)):
            raise TypeFailure(None, f"the size of {type(t).__name__}")
        raise TypeFailure(None, f"a type not measured: {type(t).__name__}")

    def record(self, t: Record) -> tuple[int, int]:
        d = t.definition
        if d.fields is None or d.node is None:
            raise TypeFailure(None, f"the size of an incomplete {d.spelling or 'struct'}")
        members = self.members(d.node)
        if d.kind == "union":
            size = max((self.measure(mt)[0] for mt, _ in members), default=0)
            align = max((self.measure(mt)[1] for mt, _ in members), default=1)
            return round_up(size, align), align
        offset = 0
        align = 1
        unit_size = unit_left = 0  # the open bit-field unit
        for mt, bits in members:
            n, a = self.measure(mt)
            align = max(align, a)
            if bits is None:
                unit_size = unit_left = 0
                offset = round_up(offset, a) + n
                continue
            if bits == 0:
                unit_size = unit_left = 0
                offset = round_up(offset, a)
                continue
            if unit_size == n and unit_left >= bits:
                unit_left -= bits
                continue
            offset = round_up(offset, a) + n
            unit_size, unit_left = n, n * 8 - bits
        return round_up(offset, align), align

    def members(self, node) -> list[tuple[CType, int | None]]:
        """A struct's members in order (anonymous ones' as one member each):
        each type and bit-width (None for an ordinary member)."""
        out = []
        for decl in node.decls or []:
            if isinstance(decl, c_ast.StaticAssert):
                continue
            t = self.tp.type_of_declarator(decl.type)
            bits = self.value(decl.bitsize) if decl.bitsize is not None else None
            out.append((t, bits))
        return out


def round_up(n: int, a: int) -> int:
    return (n + a - 1) // a * a


def char_value(text: str) -> int:
    body = text[1:-1]
    if body.startswith("\\"):
        escapes = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, "'": 39, '"': 34, "a": 7, "b": 8, "f": 12, "v": 11}
        if body[1] == "x":
            return int(body[2:], 16)
        if body[1].isdigit():
            return int(body[1:], 8)
        return escapes[body[1]]
    return ord(body)


def split_hex_escapes(value: str) -> str:
    """`value` (string literals) with each hex escape that runs into the hex digits after it ended where
    a byte ends: the parser joins adjacent literals, so the source's "\\xbf" "A" reaches here as
    "\\xbfA", which C reads as one escape out of a char's range. Escapes that fit a byte stay."""
    out, i = [], 0
    while i < len(value):
        c = value[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        if i + 1 < len(value) and value[i + 1] == "x":
            j = i + 2
            while j < len(value) and value[j] in HEX_DIGITS:
                j += 1
            digits = value[i + 2 : j]
            if len(digits) > 2 and int(digits, 16) > 0xFF:
                out.append("\\x" + digits[:2] + '""' + digits[2:])
            else:
                out.append(value[i:j])
            i = j
            continue
        out.append(value[i : i + 2])  # any other escape: the backslash and the character it escapes
        i += 2
    return "".join(out)


HEX_DIGITS = "0123456789abcdefABCDEF"


def literal_bytes(value: str) -> int:
    """A string literal's size in bytes, its terminating zero included
    (adjacent literals already joined by the parser)."""
    size = 0
    for part in split_literals(split_hex_escapes(value)):
        body = part[1:-1]
        i = 0
        while i < len(body):
            if body[i] == "\\":
                i += 1
                if body[i] == "x":
                    i += 1
                    while i < len(body) and body[i] in "0123456789abcdefABCDEF":
                        i += 1
                elif body[i] in "01234567":
                    j = 0
                    while i < len(body) and j < 3 and body[i] in "01234567":
                        i += 1
                        j += 1
                else:
                    i += 1
            else:
                i += 1
            size += 1
    return size + 1


def split_literals(value: str) -> list[str]:
    parts, i = [], 0
    while i < len(value):
        if value[i] == '"':
            j = i + 1
            while value[j] != '"':
                j += 2 if value[j] == "\\" else 1
            parts.append(value[i : j + 1])
            i = j + 1
        else:
            i += 1
    return parts
