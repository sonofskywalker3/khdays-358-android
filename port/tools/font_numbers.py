"""Opt-in numeric runs for the original panel-counter and save/load text surfaces."""

from font_fractional import COMMON, replace

PAYLOAD = "src/engine/Obj_ForwardInnerPayload.c"
COUNTERS = {
    "src/overlays/scenes/ov008_camp_menu/Ov008_DrawRowCountDigits.c": "Ov008",
    "src/overlays/screens/ov025_camp_menu_2/Ov025_DrawRowCountDigits.c": "Ov025",
}
TIMES = {
    "src/overlays/scenes/ov000_title/Ov000_DrawSaveSlotText.c": "Ov000",
    "src/overlays/scenes/ov008_camp_menu/Ov008_DrawSaveSlotTexts.c": "Ov008",
    "src/overlays/screens/ov025_camp_menu_2/Ov025_DrawSaveSlotTexts.c": "Ov025",
    "src/overlays/scenes/ov009_camp_save/Ov009_SaveMenu_DrawSummaries.c": "Ov009",
}

DECLARATIONS = """
extern int HD_FontCounter(void *surface, int row, int placed, int owned);
extern int HD_FontTime(void *surface, int layout, int x, int y, unsigned hours, unsigned minutes, unsigned seconds, const unsigned short *separator);
"""

# These adapters use the same owner/font chains as Obj_ForwardInnerPayload and Text_DrawDirectional_2.
HELPERS = """
extern int khdays_ui_text_numeric(const void *font, int layout);
extern double HD_FontDrawGlyph(int owner, int *font, double x, double y, int colour, unsigned short ch);
extern unsigned short NNS_G2dFontFindGlyphIndex(int *font, unsigned ch);
extern char *NNS_G2dFontGetCharWidthsFromIndex(int *font, unsigned index);

static int HD_Number(unsigned short *text, unsigned value, int minimum)
{
    unsigned short reverse[10];
    int count = 0, i;
    do { reverse[count++] = (unsigned short)('0' + value % 10); value /= 10; } while (value);
    while (count < minimum) reverse[count++] = '0';
    for (i = 0; i < count; ++i) text[i] = reverse[count - i - 1];
    return count;
}

static void HD_NumberRun(int owner, int *font, const unsigned short *text, int count,
                         double right, double y, int spacing, int split, int counter)
{
    double positions[64], cursor = 0;
    int i, pass;
    for (i = 0; i < count; ++i) {
        unsigned glyph = NNS_G2dFontFindGlyphIndex(font, text[i]);
        const signed char *metrics;
        if (glyph == 0xffff) glyph = *(unsigned short *)(*font + 2);
        metrics = (const signed char *)NNS_G2dFontGetCharWidthsFromIndex(font, glyph);
        if (i) cursor += khdays_ui_text_kerning_f(font, text[i - 1], text[i]);
        positions[i] = cursor;
        cursor += khdays_ui_text_metric_f(font, glyph, 2, metrics[2]) + spacing;
    }
    for (pass = 0; pass < 2; ++pass) {
        for (i = 0; i < count; ++i) {
            int colour = counter ? (i < split ? 0xf2 : 0xf0) + pass : (pass ? 2 : 1);
            HD_FontDrawGlyph(owner, font, right - cursor + spacing + positions[i] + (pass ? 0 : 1),
                              y + (pass ? 0 : 1), colour, text[i]);
        }
    }
}

int HD_FontCounter(void *surface, int row, int placed, int owned)
{
    int *object = (int *)surface;
    int *font = (int *)object[0];
    unsigned short text[24];
    int count = 0, split = 0;
    if (!khdays_ui_text_numeric(font, 0) || owned < 0 || (placed >= 0 && placed > owned)) return 0;
    if (placed >= 0) {
        count = HD_Number(text, (unsigned)(owned - placed), 1);
        text[count++] = '/';
        split = count;
    }
    count += HD_Number(text + count, (unsigned)owned, 1);
    HD_NumberRun(object[6] + 8, font, text, count, 125, row * 16 + 3, 0, split, 1);
    return 1;
}

int HD_FontTime(void *surface, int layout, int x, int y, unsigned hours, unsigned minutes,
                unsigned seconds, const unsigned short *separator)
{
    int *ctx = (int *)((char *)surface + 0x1c);
    int *font = (int *)ctx[1];
    unsigned short text[64];
    int count, i, length = 0;
    if (!khdays_ui_text_numeric(font, layout)) return 0;
    while (length < 8 && separator[length] && separator[length] != 10) ++length;
    if (!length || length == 8 || hours > 65535 || minutes > 59 || seconds > 59) return 0;
    count = HD_Number(text, hours, 2);
    for (i = 0; i < length; ++i) text[count++] = separator[i];
    count += HD_Number(text + count, minutes, 2);
    for (i = 0; i < length; ++i) text[count++] = separator[i];
    count += HD_Number(text + count, seconds, 2);
    HD_NumberRun(ctx[0], font, text, count, x + 193, y, ctx[2], 0, 0);
    return 1;
}
"""


def patch(text: str, rel: str) -> str:
    if rel == PAYLOAD:
        return COMMON + text + HELPERS
    if rel in COUNTERS:
        prefix = COUNTERS[rel]
        text = replace(
            text,
            "    nX = OWNED_X;",
            f"    if (HD_FontCounter(&pCtx->panelSurfaceA, nRow, nPlaced, nOwned)) {{\n"
            f"        pCtx->panelSurfaceA.pPixels = {prefix}_GetCtxBlock968c();\n        return;\n    }}\n"
            "    nX = OWNED_X;",
        )
        return DECLARATIONS + text
    if rel not in TIMES:
        return text
    prefix = TIMES[rel]
    if prefix == "Ov000":
        text = replace(text, "    int slot;", "    int slot;\n    int hdTime;")
        before = "            for (k = 0; k < 7; k++) {"
        after = """            hdTime = HD_FontTime(data_ov000_0205ac24->aTextObject, 1, bx,
                by + 0x17 + (data_ov000_0205ac24->bExtraEntryActive ? 8 : 0),
                wHours, bMinutes, bSeconds, Ov000_GetVarRecordByIndex(data_ov000_0205ac24->aVarTable, 0xb));
            for (k = 0; k < 7; k++) {"""
        text = replace(text, before, after)
        text = replace(
            text,
            "                dx = 0x1b;",
            "                if (hdTime && k >= 2) continue;\n                dx = 0x1b;",
        )
    elif prefix == "Ov009":
        text = replace(text, "    int rowIndex;", "    int rowIndex;\n    int hdTime;")
        text = replace(
            text,
            "            itemIndex = 0;",
            "            hdTime = HD_FontTime(ctx->renderer, 2, x, drawY + 23, hours, minutes, seconds,\n"
            "                Ov009_GetVarRecordByIndex(ctx->varRecords, 8));\n            itemIndex = 0;",
        )
        text = replace(
            text,
            "                textX = 27;",
            "                if (hdTime && itemIndex >= 2) { ++itemIndex; continue; }\n                textX = 27;",
        )
    else:
        text = replace(text, "    int nSlot;", "    int nSlot;\n    int hdTime;")
        text = replace(
            text,
            "            for (i = 0; i < 7; i++) {",
            "            hdTime = HD_FontTime(pMenu->slotTextSurface, 2, nX, nY + 0x17, nHours, nMinutes, nSeconds,\n"
            f"                {prefix}_GetVarRecordByIndex(pMenu->varRecords, 8));\n"
            "            for (i = 0; i < 7; i++) {",
        )
        text = replace(
            text,
            "                nCol = SLOT_COL_BASE;",
            "                if (hdTime && i >= 2) continue;\n                nCol = SLOT_COL_BASE;",
        )
    return DECLARATIONS + text
