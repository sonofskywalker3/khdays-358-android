package com.sonofskywalker3.khdays358.setup

/**
 * The little JSON the setup reads and writes: supported_roms.json and the extraction's manifests.
 * Values are [Map] (insertion-ordered), [List], [String], [Long], [Double], [Boolean] and null.
 * [write] lays JSON out as Python's `json.dumps(value, indent=2, ensure_ascii=False)` does, so the
 * manifests read the same as the PC tools'. Kept here rather than org.json so the JVM tests run the
 * same code as the phone.
 */
internal object Json {
    private const val INDENT = "  "
    private const val FIRST_PRINTABLE = 0x20

    fun parse(text: String): Any? {
        val reader = Reader(text)
        val value = reader.value()
        reader.skipSpace()
        if (!reader.atEnd()) {
            throw IllegalArgumentException("JSON: unexpected text at ${reader.position}.")
        }
        return value
    }

    fun write(value: Any?): String = StringBuilder().also { append(it, value, "") }.toString()

    private fun append(out: StringBuilder, value: Any?, indent: String) {
        when (value) {
            null -> out.append("null")
            is Boolean -> out.append(if (value) "true" else "false")
            is Int, is Long -> out.append(value.toString())
            is Number -> out.append(value.toDouble().toString())
            is String -> appendString(out, value)
            is Map<*, *> -> appendContainer(out, value.entries, indent, '{', '}') { entry, inner ->
                appendString(out, entry.key.toString())
                out.append(": ")
                append(out, entry.value, inner)
            }
            is Iterable<*> -> appendContainer(out, value.toList(), indent, '[', ']') { item, inner ->
                append(out, item, inner)
            }
            else -> appendString(out, value.toString())
        }
    }

    private fun <T> appendContainer(
        out: StringBuilder,
        items: Collection<T>,
        indent: String,
        open: Char,
        close: Char,
        item: (T, String) -> Unit,
    ) {
        out.append(open)
        if (items.isEmpty()) {
            out.append(close)
            return
        }
        val inner = indent + INDENT
        items.forEachIndexed { index, value ->
            out.append(if (index == 0) "\n" else ",\n").append(inner)
            item(value, inner)
        }
        out.append('\n').append(indent).append(close)
    }

    private fun appendString(out: StringBuilder, text: String) {
        out.append('"')
        for (c in text) {
            when (c) {
                '"' -> out.append("\\\"")
                '\\' -> out.append("\\\\")
                '\n' -> out.append("\\n")
                '\r' -> out.append("\\r")
                '\t' -> out.append("\\t")
                '\b' -> out.append("\\b")
                '\u000C' -> out.append("\\f")
                else -> if (c.code < FIRST_PRINTABLE) {
                    out.append("\\u").append(c.code.toString(16).padStart(4, '0'))
                } else {
                    out.append(c)
                }
            }
        }
        out.append('"')
    }

    /** A recursive-descent reader over [text]. */
    private class Reader(private val text: String) {
        var position = 0
            private set

        fun atEnd(): Boolean = position >= text.length

        fun skipSpace() {
            while (!atEnd() && text[position].isWhitespace()) position++
        }

        fun value(): Any? {
            skipSpace()
            if (atEnd()) fail("value expected")
            return when (val c = text[position]) {
                '{' -> objectValue()
                '[' -> arrayValue()
                '"' -> stringValue()
                't' -> literal("true", true)
                'f' -> literal("false", false)
                'n' -> literal("null", null)
                else -> if (c == '-' || c.isDigit()) numberValue() else fail("unexpected '$c'")
            }
        }

        private fun objectValue(): Map<String, Any?> {
            val result = LinkedHashMap<String, Any?>()
            position++
            skipSpace()
            if (peek('}')) return result
            while (true) {
                skipSpace()
                val key = stringValue()
                skipSpace()
                expect(':')
                result[key] = value()
                skipSpace()
                if (peek('}')) return result
                expect(',')
            }
        }

        private fun arrayValue(): List<Any?> {
            val result = ArrayList<Any?>()
            position++
            skipSpace()
            if (peek(']')) return result
            while (true) {
                result.add(value())
                skipSpace()
                if (peek(']')) return result
                expect(',')
            }
        }

        private fun stringValue(): String {
            expect('"')
            val out = StringBuilder()
            while (true) {
                if (atEnd()) fail("unterminated string")
                val c = text[position++]
                when (c) {
                    '"' -> return out.toString()
                    '\\' -> {
                        if (atEnd()) fail("unterminated escape")
                        when (val e = text[position++]) {
                            '"', '\\', '/' -> out.append(e)
                            'b' -> out.append('\b')
                            'f' -> out.append('\u000C')
                            'n' -> out.append('\n')
                            'r' -> out.append('\r')
                            't' -> out.append('\t')
                            'u' -> {
                                if (position + 4 > text.length) fail("truncated \\u escape")
                                out.append(text.substring(position, position + 4).toInt(16).toChar())
                                position += 4
                            }
                            else -> fail("bad escape '\\$e'")
                        }
                    }
                    else -> out.append(c)
                }
            }
        }

        private fun numberValue(): Any {
            val start = position
            while (!atEnd() && text[position] in NUMBER_CHARS) position++
            val token = text.substring(start, position)
            return token.toLongOrNull() ?: token.toDoubleOrNull() ?: fail("bad number '$token'")
        }

        private fun literal(word: String, value: Any?): Any? {
            if (!text.startsWith(word, position)) fail("bad literal")
            position += word.length
            return value
        }

        private fun peek(c: Char): Boolean {
            if (!atEnd() && text[position] == c) {
                position++
                return true
            }
            return false
        }

        private fun expect(c: Char) {
            if (!peek(c)) fail("'$c' expected")
        }

        private fun fail(what: String): Nothing = throw IllegalArgumentException("JSON: $what at $position.")
    }

    private const val NUMBER_CHARS = "0123456789+-.eE"
}
