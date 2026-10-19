// The system's dialog choosing where an export is saved.
#pragma once

#include "app.h"

#include <string>
#include <vector>

namespace studio {

// Opens it with `name` in its name box, showing the files of `pattern`
// (`png`, `gltf`); the path chosen goes to `queue` (one of app's dialog
// lists). Replacing a file is always asked first.
void choose_save_file(App& app, const std::string& name, const char* filter, const char* pattern,
                      std::vector<std::string>& queue);

}  // namespace studio
