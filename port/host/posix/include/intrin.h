/* MSVC's intrinsics the runtime uses, as clang's builtins (off Windows only). */
#ifndef KHDAYS_POSIX_INTRIN_H
#define KHDAYS_POSIX_INTRIN_H
#define _ReturnAddress() __builtin_return_address(0)
/* the highest set bit's index into *index; 0 when mask has none */
#define _BitScanReverse(index, mask)                                                                                   \
    __extension__({                                                                                                    \
        const unsigned int khdays_mask_ = (unsigned int)(mask);                                                        \
        if (khdays_mask_ != 0) {                                                                                       \
            *(index) = 31u - (unsigned int)__builtin_clz(khdays_mask_);                                                \
        }                                                                                                              \
        (unsigned char)(khdays_mask_ != 0);                                                                            \
    })
#endif
