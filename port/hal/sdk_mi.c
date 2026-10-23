/* NitroSDK MI (memory interface) routines the decompilation keeps as ARM
 * assembly (libs/nitro/mi/asm_stubs). Argument order follows that assembly
 * (e.g. MI_Copy48B reads r0 and writes r1), not the parameter names some
 * callers gave their extern declarations. */
#include "hal.h"
#include "../runtime/core/io.h"
#include "../runtime/core/origin.h"
#include "../runtime/core/runtime.h"

extern unsigned int khdays_ui_text_copy_owner(const void *source, const void *destination, unsigned int size);
extern void khdays_ui_text_memory_copy(const void *source, void *destination, unsigned int size, unsigned int owner);
extern void khdays_ui_text_memory_clear(const void *destination, unsigned int size);

void MI_CpuFill8(void *dest, u8 data, u32 size)
{
    const u32 written = size;
    u8 *d = (u8 *)dest;
    while (size-- != 0) {
        *d++ = data;
    }
    khdays_ui_text_memory_clear(dest, written);
}

void MI_CpuCopy8(const void *src, void *dest, u32 size)
{
    const u32 owner = khdays_ui_text_copy_owner(src, dest, size);
    const u8 *s = (const u8 *)src;
    u8 *d = (u8 *)dest;
    /* the bytes' origin followed for a tile or more (the field HUD's portraits are
     * copied row by row into its sheet so); the many small copies are left out,
     * and whoever reads a name checks the bytes (origin.h) */
    if (size >= 32) {
        khdays_origin_copy((u32)src, (u32)dest, size);
    }
    /* Byte by byte, forward, like the assembly (overlap behaves the same). */
    const u32 written = size;
    while (size-- != 0) {
        *d++ = *s++;
    }
    khdays_ui_text_memory_copy(src, dest, written, owner);
}

void MIi_CpuClear16(u16 data, void *dest, u32 size)
{
    volatile u16 *d = (volatile u16 *)dest;
    khdays_origin_clear((u32)dest, size);
    for (u32 i = 0; i < size / 2; ++i) {
        d[i] = data;
    }
    khdays_ui_text_memory_clear(dest, size & ~1u);
}

/* The port's geometry takes GXFIFO's words as a block (gfx/g3d.h). */
extern void khdays_g3d_words(const u32 *words, u32 count);

static int touches_registers(u32 address, u32 size);
static void copy_registers(u32 src, u32 dest, u32 size, int unit);

void MIi_CpuCopy16(const void *src, void *dest, u32 size)
{
    const u32 owner = khdays_ui_text_copy_owner(src, dest, size & ~1u);
    const volatile u16 *s = (const volatile u16 *)src;
    volatile u16 *d = (volatile u16 *)dest;
    khdays_origin_copy((u32)src, (u32)dest, size);
    if (touches_registers((u32)src, size) || touches_registers((u32)dest, size)) {
        copy_registers((u32)src, (u32)dest, size & ~1u, 2);
        return;
    }
    for (u32 i = 0; i < size / 2; ++i) {
        d[i] = s[i];
    }
    khdays_ui_text_memory_copy(src, dest, size & ~1u, owner);
}

/* A copy from or to registers (a matrix to the geometry's command ports, the
 * clip matrix read back): each word through the register model, as the
 * game's own loads and stores would reach it, without a fault per word. */
static int touches_registers(u32 address, u32 size)
{
    return size != 0 && (khdays_io_is_register(address) || khdays_io_is_register(address + size - 1));
}

/* A unit of plain memory (the copies' units are aligned to their size). */
static u32 load_unit(u32 address, int unit)
{
    return unit == 1   ? *(const volatile u8 *)address
           : unit == 2 ? *(const volatile u16 *)address
                       : *(const volatile u32 *)address;
}

static void store_unit(u32 address, int unit, u32 value)
{
    if (unit == 1) {
        *(volatile u8 *)address = (u8)value;
    } else if (unit == 2) {
        *(volatile u16 *)address = (u16)value;
    } else {
        *(volatile u32 *)address = value;
    }
}

static void copy_registers(u32 src, u32 dest, u32 size, int unit)
{
    for (u32 i = 0; i < size; i += (u32)unit) {
        const u32 value = khdays_io_is_register(src + i) ? khdays_io_load(src + i, unit) : load_unit(src + i, unit);
        if (khdays_io_is_register(dest + i)) {
            khdays_io_store(dest + i, unit, value);
        } else {
            store_unit(dest + i, unit, value);
        }
    }
}

void MIi_CpuCopy32(const void *src, void *dest, u32 size)
{
    const u32 owner = khdays_ui_text_copy_owner(src, dest, size & ~3u);
    const volatile u32 *s = (const volatile u32 *)src;
    volatile u32 *d = (volatile u32 *)dest;
    if ((u32)src >= 0xffff0000u) {
        /* the ARM9 BIOS, in no process's memory: the port reads it (boot.c) */
        khdays_bios_read((u32)src, dest, size);
        return;
    }
    khdays_origin_copy((u32)src, (u32)dest, size);
    if (touches_registers((u32)src, size) || touches_registers((u32)dest, size)) {
        copy_registers((u32)src, (u32)dest, size & ~3u, 4);
        return;
    }
    for (u32 i = 0; i < size / 4; ++i) {
        d[i] = s[i];
    }
    khdays_ui_text_memory_copy(src, dest, size & ~3u, owner);
}

/* Writes every word to the same destination: a FIFO port. GXFIFO's words go
 * to the port's geometry as a block (gfx/g3d.h), another port's to the
 * register model directly -- neither through the trap. */
void MIi_CpuSend32(const void *src, volatile void *dest, u32 size)
{
    const volatile u32 *s = (const volatile u32 *)src;
    volatile u32 *d = (volatile u32 *)dest;
    if ((u32)dest == 0x04000400u) {
        khdays_g3d_words((const u32 *)src, size / 4);
        return;
    }
    if (khdays_io_is_register((u32)dest)) {
        for (u32 i = 0; i < size / 4; ++i) {
            khdays_io_store((u32)dest, 4, s[i]);
        }
        return;
    }
    for (u32 i = 0; i < size / 4; ++i) {
        *d = s[i];
    }
}

void MIi_CpuClearFast(u32 data, void *dest, u32 size)
{
    volatile u32 *d = (volatile u32 *)dest;
    khdays_origin_clear((u32)dest, size);
    for (u32 i = 0; i < size / 4; ++i) {
        d[i] = data;
    }
    khdays_ui_text_memory_clear(dest, size & ~3u);
}

void MIi_CpuCopyFast(const void *src, void *dest, u32 size)
{
    MIi_CpuCopy32(src, dest, size);
}

void MI_Copy36B(const void *src, void *dest)
{
    MIi_CpuCopy32(src, dest, 36);
}

void MI_Copy48B(const void *src, void *dest)
{
    MIi_CpuCopy32(src, dest, 48);
}

void MI_Copy64B(const void *src, void *dest)
{
    MIi_CpuCopy32(src, dest, 64);
}

void MI_Zero36B(void *dest)
{
    MIi_CpuClearFast(0, dest, 36);
}

/* `swp`: store the new value and return the old one, atomically. The native
 * game runs on one thread, so a plain exchange is atomic enough. */
u32 MI_SwapWord(u32 value, volatile u32 *destination)
{
    const u32 old = *destination;
    *destination = value;
    return old;
}

/* crt0's INITi_CpuClear32 and its two copies. */
static void clear32(u32 data, void *dest, u32 size)
{
    volatile u32 *d = (volatile u32 *)dest;
    for (u32 i = 0; i < size / 4; ++i) {
        d[i] = data;
    }
    khdays_ui_text_memory_clear(dest, size & ~3u);
}

void INITi_CpuClear32(u32 data, void *dest, u32 size)
{
    clear32(data, dest, size);
}
void INITi_CpuClear32_0x01ff86fc(u32 data, void *dest, u32 size)
{
    clear32(data, dest, size);
}
void INITi_CpuClear32_0x0200093c(u32 data, void *dest, u32 size)
{
    clear32(data, dest, size);
}

/* MI_ReadUncompLZ8 (func_02004484): the streaming LZ decoder, fed one chunk
 * at a time; returns the bytes still to write (0 = done). A register-for-
 * register translation -- the labels are the assembly's, and the context
 * carries the decoder's registers between calls. */
typedef struct {
    u8 *destp;     /* r3 */
    s32 destCount; /* r4 */
    s32 length;    /* r7 */
    u16 destTmp;
    u8 destTmpCnt;
    u8 flags;     /* r5 */
    u8 flagIndex; /* r6 */
    u8 lengthFlg; /* r8 */
    u8 exFormat;  /* r11 */
    u8 padding;
} MIUncompContextLZ;

s32 func_02004484(MIUncompContextLZ *context, const u8 *data, u32 len)
{
    u8 *dest = context->destp;
    s32 destCount = context->destCount;
    u32 flags = context->flags;
    u32 flagIndex = context->flagIndex;
    s32 length = context->length;
    u32 lengthFlg = context->lengthFlg;
    const u32 exFormat = context->exFormat;
    u32 offset;

l21:
    if (destCount <= 0)
        goto l29;
    if (flagIndex == 0)
        goto l28;
l22:
    if (len == 0)
        goto l29;
    if ((flags & 0x80) == 0) {
        --destCount;
        --len;
        *dest++ = *data++;
        goto l26;
    }
l23:
    if (lengthFlg == 0)
        goto l24;
    if (exFormat != 1)
        goto l23_9;
    if (--lengthFlg == 0)
        goto l23_7;
    if (lengthFlg == 1)
        goto l23_6;
    length = *data++;
    if ((length & 0xe0) == 0)
        goto l23_4;
    length += 0x10;
    lengthFlg = 0;
    goto l23_10;
l23_4:
    if ((length & 0x10) == 0)
        goto l23_5;
    length = 0x1110 + ((length & 0xf) << 16);
    goto l23_8;
l23_5:
    length = 0x110 + ((length & 0xf) << 8);
    lengthFlg = 1;
    goto l23_8;
l23_6:
    length += *data++ << 8;
    goto l23_8;
l23_7:
    length += *data++;
    goto l23_10;
l23_8:
    if (--len == 0)
        goto l29;
    goto l23;
l23_9:
    length = *data++ + 0x30;
    lengthFlg = 0;
l23_10:
    if (--len == 0)
        goto l29;
l24:
    offset = (((u32)length & 0xf) << 8 | *data++) + 1;
    lengthFlg = 3;
    --len;
    length >>= 4; /* asr */
    if (length == 0)
        goto l26;
    do {
        const u8 value = *(dest - offset);
        --destCount;
        *dest++ = value;
    } while (--length > 0);
l26:
    if (destCount == 0)
        goto l29;
    flags <<= 1;
    if (--flagIndex != 0)
        goto l22;
l28:
    if (len == 0)
        goto l29;
    flags = *data++;
    flagIndex = 8;
    --len;
    goto l21;
l29:
    context->destp = dest;
    context->destCount = destCount;
    context->flags = (u8)flags;
    context->flagIndex = (u8)flagIndex;
    context->length = length;
    context->lengthFlg = (u8)lengthFlg;
    context->exFormat = (u8)exFormat;
    return destCount;
}
