#include "save_dialog.h"

#include "core/files/paths.h"

#include <functional>
#include <memory>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace studio {

namespace {

using Chosen = std::function<void(const std::string&)>;

#ifdef _WIN32
// Not SDL's: it leaves a name typed without the extension as it is, so the
// export adding it would replace a file without asking.
void show(HWND owner, const std::wstring& filter, const std::wstring& pattern, const std::wstring& name,
          const Chosen& chosen) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return;
    IFileSaveDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_OVERWRITEPROMPT);
        const std::wstring spec = L"*." + pattern;
        const COMDLG_FILTERSPEC types[] = {{filter.c_str(), spec.c_str()}};
        dialog->SetFileTypes(1, types);
        dialog->SetDefaultExtension(pattern.c_str());
        dialog->SetFileName(name.c_str());
        IShellItem* item = nullptr;
        PWSTR path = nullptr;
        if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item)) &&
            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            chosen(native_u8(fs::path(path)));
        if (path != nullptr)
            CoTaskMemFree(path);
        if (item != nullptr)
            item->Release();
        dialog->Release();
    }
    CoUninitialize();
}

// On a thread of its own, as SDL's: the window keeps drawing behind it.
void open(SDL_Window* window, const char* filter, const char* pattern, const std::string& name, Chosen chosen) {
    const auto owner = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    std::thread(show, owner, path_u8(filter).wstring(), path_u8(pattern).wstring(), path_u8(name).wstring(),
                std::move(chosen))
        .detach();
}
#else
void open(SDL_Window* window, const char* filter, const char* pattern, const std::string& name, Chosen chosen) {
    struct Pending {  // what SDL reads until it calls back
        Chosen chosen;
        std::string filter, pattern;
        SDL_DialogFileFilter filters[1];
    };
    auto* p = new Pending{std::move(chosen), filter, pattern, {}};
    p->filters[0] = {p->filter.c_str(), p->pattern.c_str()};
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, p->filters);
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, name.c_str());
    SDL_ShowFileDialogWithProperties(
        SDL_FILEDIALOG_SAVEFILE,
        [](void* user, const char* const* files, int) {
            const std::unique_ptr<Pending> p(static_cast<Pending*>(user));
            if (files != nullptr && files[0] != nullptr)
                p->chosen(files[0]);
        },
        p, props);
    SDL_DestroyProperties(props);
}
#endif

}  // namespace

void choose_save_file(App& app, const std::string& name, const char* filter, const char* pattern,
                      std::vector<std::string>& queue) {
    open(app.window, filter, pattern, name, [a = &app, q = &queue](const std::string& file) {
        std::lock_guard lock(a->dialog_mutex);
        q->push_back(file);
    });
}

}  // namespace studio
