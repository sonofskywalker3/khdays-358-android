/* What the game's C rewritten by port/tools/references needs: the reference type, the window's conversions,
 * and the numbers the build gives the data area and the function references.
 * The rewritten C is a whole translation unit already preprocessed, so this
 * is the one header it includes. */
#ifndef KHDAYS_REFERENCES_H
#define KHDAYS_REFERENCES_H

/* A pointer of the game's: 4 bytes, the DS address it had. */
typedef unsigned int khdays_ref;

/* The host address of reference 0 (runtime/core/window.c). */
extern unsigned char *khdays_window;
#define KHDAYS_W(reference) ((void *)(khdays_window + (khdays_ref)(reference)))

/* A host address in the window as a reference (stops the game, naming the
 * caller, when it is not in it) -- what the port's side hands the game. */
khdays_ref khdays_reference(const void *host);
#define KHDAYS_REF(host) khdays_reference((const void *)(host))

/* The native function of a function reference (stops the game, naming it,
 * when there is none). */
typedef void (*khdays_function_pointer)(void);
khdays_function_pointer khdays_function(khdays_ref reference);
#define KHDAYS_FN(reference) khdays_function((khdays_ref)(reference))

/* What the game hands the port's side: a reference as its host address, a
 * function reference as its native function -- 0 as NULL either way. */
static __inline void *khdays_host(khdays_ref reference)
{
    return reference ? (void *)(khdays_window + reference) : (void *)0;
}
static __inline khdays_function_pointer khdays_host_function(khdays_ref reference)
{
    return reference ? khdays_function(reference) : (khdays_function_pointer)0;
}

/* Where the records the runtime walks go: the data initializers (runtime/core/data_init.c) and the
 * numbered function references (runtime/core/window.c). MSVC groups a section's $-parts in name order;
 * an ELF linker gives a section named as a C identifier its __start_ and __stop_ symbols. */
#if defined(_MSC_VER)
#pragma section(".khdi$m", read, write)
#pragma section(".khfr$m", read)
#define KHDAYS_IN_DATA_INITS __declspec(allocate(".khdi$m"))
#define KHDAYS_IN_FUNCTION_RECORDS __declspec(allocate(".khfr$m"))
#else
#define KHDAYS_IN_DATA_INITS __attribute__((used, section("khdi")))
#define KHDAYS_IN_FUNCTION_RECORDS __attribute__((used, section("khfr")))
#endif

/* A function reference the build numbers (one with no DS address: a static
 * function, the port's), and its native function: in section .khfr$m. */
#pragma pack(push, 8) /* as runtime/core/window.c reads them, whatever the files' packing */
struct khdays_function_record {
    khdays_ref reference;
    khdays_function_pointer function;
};
#pragma pack(pop)

/* A member's offset, without <stddef.h> (whose size_t is not the game's). */
#define KHDAYS_OFFSETOF(type, member) ((khdays_ref)(unsigned long long)&(((type *)0)->member))

/* The data area's objects and the function references the build numbers
 * (build/port/gen-x64/khdays_data_area.h). */
#define KHDAYS_DATA(key) KHDAYS_DATA_##key
#define KHDAYS_FREF(name) KHDAYS_FREF_##name
#include "khdays_data_area.h"

#endif
