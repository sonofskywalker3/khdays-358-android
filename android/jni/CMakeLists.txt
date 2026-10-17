# Gradle's entry point: port/'s CMake tree, which on Android builds the game as the app's libmain.so
# next to a shared libSDL3.so that SDLActivity loads first.
cmake_minimum_required(VERSION 3.25)
project(khdays_android LANGUAGES C CXX)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../port" port)
