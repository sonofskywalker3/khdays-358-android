# Applies this folder's patch to SDL's unpacked source (FetchContent's PATCH_COMMAND, run in that source).
# The patch carries its revision (KHDAYS_PIPELINE_CACHE_PATCH_<n>): this revision already there, nothing is
# done; an older one there, the source must be unpacked again (delete the build's _deps/sdl3-src and -subbuild).
set(revision "KHDAYS_PIPELINE_CACHE_PATCH_2")
find_package(Git REQUIRED)
file(READ "src/gpu/vulkan/SDL_gpu_vulkan.c" vulkan_source)
string(FIND "${vulkan_source}" "${revision}" current)
string(FIND "${vulkan_source}" "KHDAYS_PIPELINE_CACHE_MAGIC" any)
if(NOT current EQUAL -1)
    return()
endif()
if(NOT any EQUAL -1)
    message(FATAL_ERROR "SDL: an older vulkan-pipeline-cache.patch is in this source; delete _deps/sdl3-src and _deps/sdl3-subbuild and configure again")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace -p1 "${CMAKE_CURRENT_LIST_DIR}/vulkan-pipeline-cache.patch"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "SDL: vulkan-pipeline-cache.patch did not apply")
endif()
