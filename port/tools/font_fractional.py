"""Keep the public DS integer ABI; use fractional coordinates inside font layout."""

import re

COMMON = """
extern double khdays_ui_text_metric_f(const void *font, int glyph, int field, double fallback);
extern double khdays_ui_text_kerning_f(const void *font, int left, int right);
extern double khdays_ui_text_extent_f(const void *font, int glyph, int left, int width, int advance);
extern int khdays_ui_text_round(double value);
extern void khdays_ui_text_position(const void *font, double x, double y);
extern double khdays_ui_text_pen_x(int x);
"""


def replace(text: str, before: str, after: str) -> str:
    if before not in text:
        raise SystemExit(f"fractional font hook not found: {before}")
    return text.replace(before, after)


def patch(text: str, rel: str) -> str:
    name = rel.rsplit("/", 1)[-1]
    if name == "Text_DrawGlyph.c":
        signature = "int Text_DrawGlyph(int owner, int *font, int x, int y, int p5, unsigned short ch)"
        text = replace(
            text,
            signature,
            "double HD_FontDrawGlyph(int owner, int *font, double x, double y, int p5, unsigned short ch)",
        )
        text = text.replace("khdays_ui_text_metric(", "khdays_ui_text_metric_f(")
        text = replace(text, "    (*(int (**)", "    khdays_ui_text_position(font, x, y);\n    (*(int (**)")
        text = replace(
            text,
            "(owner, font, x, y, p5, &gd);",
            "(owner, font, khdays_ui_text_round(x), khdays_ui_text_round(y), p5, &gd);\n    khdays_ui_text_position((void *)0, 0, 0);",
        )
        text += (
            "\n"
            + signature
            + " { return khdays_ui_text_round(HD_FontDrawGlyph(owner, font, khdays_ui_text_pen_x(x), y, p5, ch)); }\n"
        )
    elif name == "Text_DrawLine.c":
        signature = "void Text_DrawLine(int *ctx, int x, int y, int p5, int s, int *pNext, TextDir dir)"
        text = replace(
            text,
            signature,
            "void HD_FontDrawLine(int *ctx, double x, double y, int p5, int s, int *pNext, TextDir dir)",
        )
        text = replace(text, "int adv, w;", "double adv, w;")
        text = replace(text, "adv = Text_DrawGlyph(", "adv = HD_FontDrawGlyph(")
        text = text.replace("khdays_ui_text_kerning(", "khdays_ui_text_kerning_f(")
        text = (
            "extern double HD_FontDrawGlyph(int owner, int *font, double x, double y, int colour, unsigned short ch);\n"
            + text
        )
        text += "\n" + signature + " { HD_FontDrawLine(ctx, x, y, p5, s, pNext, dir); }\n"
    elif name == "NNSi_G2dFontGetStringWidth.c":
        text = replace(text, "inline int NNS_G2dFontGetCharWidth", "inline double NNS_G2dFontGetCharWidth")
        text = replace(text, "static int HD_MeasureLine", "static double HD_MeasureLine")
        text = replace(text, "int *leftOut", "double *leftOut")
        for var in ("width", "right", "low", "advance", "extent", "bearing"):
            text = re.sub(r"\bint " + var + r"\b", "double " + var, text)
        for api in ("metric", "kerning", "extent"):
            text = text.replace("khdays_ui_text_" + api + "(", "khdays_ui_text_" + api + "_f(")
        text = replace(
            text,
            "return HD_MeasureLine(pFont, hSpace, str, pPos, (int *)0);",
            "return khdays_ui_text_round(HD_MeasureLine(pFont, hSpace, str, pPos, (double *)0));",
        )
        text = replace(
            text,
            "int HD_FontLineLeft(const NNSG2dFont *font, int spacing, const void *str)",
            "double HD_FontLineLeft(int font, int spacing, int str)",
        )
        text = replace(
            text,
            "HD_MeasureLine(font, spacing, str, (const void **)0, &low)",
            "HD_MeasureLine((const NNSG2dFont *)font, spacing, (const void *)str, (const void **)0, &low)",
        )
        text += "\ndouble HD_FontLineWidth(int font, int spacing, int str, int *next) { return HD_MeasureLine((const NNSG2dFont *)font, spacing, (const void *)str, (const void **)next, (double *)0); }\n"
    elif name == "Text_DrawAlignedLines.c":
        text = replace(text, "extern int HD_FontLineLeft", "extern double HD_FontLineLeft")
        signature = "void Text_DrawAlignedLines(int *ctx, int x, int y, int width, int p5,\n                   unsigned int flags, int str, TextDir dir)"
        text = replace(
            text,
            signature,
            "void HD_FontDrawAlignedLines(int *ctx, double x, double y, double width, int p5, unsigned int flags, int str, TextDir dir)",
        )
        for var in ("step", "stepx", "stepy", "px", "ox", "w", "low"):
            text = re.sub(r"\bint " + var + r"\b", "double " + var, text)
        text = text.replace("khdays_ui_text_metric(", "khdays_ui_text_metric_f(")
        text = replace(
            text, "NNSi_G2dFontGetStringWidth(ctx[1], ctx[2], s, 0)", "HD_FontLineWidth(ctx[1], ctx[2], s, 0)"
        )
        text = replace(
            text,
            "w = (width + 1) / 2 - (w + 1) / 2;",
            "w = khdays_ui_text_metric_f((void *)ctx[1], -1, 4, 0) ? (width - w) / 2 : (int)(width + 1) / 2 - (int)(w + 1) / 2;",
        )
        text = replace(
            text,
            "            Text_DrawLine(ctx, px, py, p5, s, &s, dir);",
            "            HD_FontDrawLine(ctx, px, py, p5, s, &s, dir);",
        )
        at = text.index("void HD_FontDrawAlignedLines")
        text = (
            text[:at]
            + "extern void HD_FontDrawLine(int *ctx, double x, double y, int colour, int str, int *next, TextDir dir);\n"
            + text[at:]
        )
        text = "extern double HD_FontLineWidth(int font, int spacing, int str, int *next);\n" + text
        text += "\n" + signature + " { HD_FontDrawAlignedLines(ctx, x, y, width, p5, flags, str, dir); }\n"
    elif name == "NNSi_G2dFontGetTextRect.c":
        signature = "NNSG2dTextRect NNSi_G2dFontGetTextRect (const NNSG2dFont * pFont, int hSpace, int vSpace, const void * txt)"
        at = text.index(signature)
        text = (
            text[:at]
            + """
extern double HD_FontLineWidth(int font, int spacing, int str, int *next);
void HD_FontTextRect(int font, int hspace, int vspace, int txt, double *width, double *height)
{
    int lines = 0;
    double feed = khdays_ui_text_metric_f((void *)font, -1, 3, ((NNSG2dFont *)font)->pRes->linefeed);
    *width = 0;
    while (txt) {
        double w = HD_FontLineWidth(font, hspace, txt, &txt);
        if (w > *width) *width = w;
        ++lines;
    }
    *height = lines * (feed + vspace) - vspace;
    if (lines && khdays_ui_text_metric_f((void *)font, -1, 4, 0))
        *height += khdays_ui_text_metric_f((void *)font, -1, 6, feed) - feed;
}
"""
            + signature
            + """
{
    double width, height;
    NNSG2dTextRect rect;
    HD_FontTextRect((int)pFont, hSpace, vSpace, (int)txt, &width, &height);
    rect.width = khdays_ui_text_round(width);
    rect.height = khdays_ui_text_round(height);
    return rect;
}
"""
        )
    elif name == "Text_AlignAnchor.c":
        at = text.index("void Text_AlignAnchor(")
        text = (
            text[:at]
            + """
extern void HD_FontTextRect(int font, int hspace, int vspace, int txt, double *width, double *height);
extern void HD_FontDrawAlignedLines(int *ctx, double x, double y, double width, int colour, unsigned int flags, int str, AxisOffset dir);
void Text_AlignAnchor(int *ctx, int x, int y, int mode, int flags, int p6, AxisOffset off)
{
    double width, height, px = x, py = y, t;
    int sized = khdays_ui_text_metric_f((void *)ctx[1], -1, 4, 0) != 0;
    HD_FontTextRect(ctx[1], ctx[2], ctx[3], p6, &width, &height);
    if (flags & 0x10) {
        t = sized ? -width / 2 : (int)-(width + 1) / 2;
        px += t * off.dx; py += t * off.dy;
    } else if (flags & 0x20) { px -= width * off.dx; py -= width * off.dy; }
    if (flags & 2) {
        t = sized ? -height / 2 : (int)-(height + 1) / 2;
        px += t * -off.dy; py += t * off.dx;
    } else if (flags & 4) { px += height * off.dy; py -= height * off.dx; }
    HD_FontDrawAlignedLines(ctx, px, py, width, mode, flags, p6, off);
}
"""
        )
    else:
        return text
    # Integer compatibility declarations must not be renamed into floating APIs.
    text = re.sub(r"^extern int khdays_ui_text_(?:metric|kerning|extent)_f\([^\n]*\);\n", "", text, flags=re.M)
    return COMMON + text
