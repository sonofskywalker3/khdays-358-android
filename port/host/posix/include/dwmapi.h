/* dwmapi.h for the POSIX builds (Android): no desktop compositor to lock the DS's VBlank to, so
 * runtime/core/clock.c finds the timing unavailable and keeps its own rate. */
#ifndef KHDAYS_POSIX_DWMAPI_H
#define KHDAYS_POSIX_DWMAPI_H

#include <windows.h>

typedef unsigned long long QPC_TIME;

typedef struct DWM_TIMING_INFO {
    unsigned int cbSize;
    QPC_TIME qpcRefreshPeriod;
    QPC_TIME qpcVBlank;
} DWM_TIMING_INFO;

static inline HRESULT DwmGetCompositionTimingInfo(void *window, DWM_TIMING_INFO *info)
{
    (void)window;
    (void)info;
    return E_NOTIMPL;
}

#endif
