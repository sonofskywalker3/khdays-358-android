/* host/app/update_install.h on Windows. */
#include "update_install.h"

#include <windows.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOLDER L"khdays-update"
#define INSTALLER_PATTERN L"KH358-2-Days-Port-Setup-*"

/* Under Proton or Wine: Wine's ntdll.dll has wine_get_version, Windows's has not. */
static int under_wine(void)
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != NULL && GetProcAddress(ntdll, "wine_get_version") != NULL;
}

int khdays_update_install_possible(void)
{
    return !under_wine() && GetFileAttributesW(L"unins000.exe") != INVALID_FILE_ATTRIBUTES;
}

/* The folder, as a wide path with its closing backslash, into `out`: 1, or 0. */
static int folder_wide(wchar_t *out, DWORD size)
{
    const DWORD n = GetTempPathW(size, out);
    if (n == 0 || n + wcslen(FOLDER) + 2 > size) {
        return 0;
    }
    wcscat_s(out, size, FOLDER);
    CreateDirectoryW(out, NULL);
    wcscat_s(out, size, L"\\");
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

int khdays_update_install_folder(char *out, size_t size)
{
    wchar_t folder[MAX_PATH + 1];
    return folder_wide(folder, MAX_PATH + 1) &&
           WideCharToMultiByte(CP_UTF8, 0, folder, -1, out, (int)size, NULL, NULL) > 0;
}

void khdays_update_install_clean(void)
{
    wchar_t folder[MAX_PATH + 1], pattern[MAX_PATH + 64];
    WIN32_FIND_DATAW found;
    HANDLE find;
    if (!folder_wide(folder, MAX_PATH + 1)) {
        return;
    }
    swprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%s%s", folder, INSTALLER_PATTERN);
    find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        wchar_t file[MAX_PATH + 300];
        swprintf(file, sizeof(file) / sizeof(file[0]), L"%s%s", folder, found.cFileName);
        DeleteFileW(file); /* the installer still running is kept by the system */
    } while (FindNextFileW(find, &found));
    FindClose(find);
}

int khdays_update_install_run(const char *file, char *why, size_t size)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, file, -1, NULL, 0);
    const DWORD here_size = GetCurrentDirectoryW(0, NULL);
    wchar_t *wfile = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
    wchar_t *here = here_size > 0 ? (wchar_t *)malloc((size_t)here_size * sizeof(wchar_t)) : NULL;
    wchar_t *params = NULL;
    SHELLEXECUTEINFOW run;
    size_t length;
    int ok = 0;
    why[0] = '\0';
    if (wfile == NULL || here == NULL || MultiByteToWideChar(CP_UTF8, 0, file, -1, wfile, n) != n ||
        GetCurrentDirectoryW(here_size, here) == 0) {
        snprintf(why, size, "the installer's or the game's folder cannot be read");
        goto done;
    }
    /* this copy's folder (Inno Setup's /DIR), its last choices, a progress window alone, and the
     * port's own /AUTOUPDATE: no Steam steps, the game opened again at the end */
    length = wcslen(here) + 96;
    params = (wchar_t *)malloc(length * sizeof(wchar_t));
    if (params == NULL) {
        snprintf(why, size, "out of memory");
        goto done;
    }
    swprintf(params, length, L"/SILENT /NORESTART /AUTOUPDATE /DIR=\"%s\"", here);
    memset(&run, 0, sizeof(run));
    run.cbSize = sizeof(run);
    run.fMask = SEE_MASK_NOASYNC;
    run.lpVerb = L"open";
    run.lpFile = wfile;
    run.lpParameters = params;
    run.nShow = SW_SHOWNORMAL;
    ok = ShellExecuteExW(&run) != FALSE;
    if (!ok) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            snprintf(why, size, "the administrator's permission was not given");
        } else {
            snprintf(why, size, "it could not be started (error %lu)", (unsigned long)error);
        }
    }
done:
    free(params);
    free(here);
    free(wfile);
    return ok;
}
