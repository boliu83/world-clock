cmake_minimum_required(VERSION 3.20)
project(WorldClock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "WorldClock is Windows-only (Win32).")
endif()

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00   # Windows 10
    WINVER=0x0A00
)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    # Release: small, fast startup
    add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/GL>)
    add_link_options($<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

set(APP_SOURCES
    src/app.cpp
    src/theme.cpp
    src/settings.cpp
    src/tz.cpp
    src/clock_engine.cpp
    src/tray.cpp
    src/popup.cpp
    src/app.rc
)

add_executable(WorldClock WIN32 ${APP_SOURCES})

target_include_directories(WorldClock PRIVATE src)

# Windows 10 SDK ships icu.lib exposing unversioned ICU C API (icu.dll).
target_link_libraries(WorldClock PRIVATE
    user32 gdi32 shell32 dwmapi comctl32 uxtheme
    advapi32 shlwapi ole32 version icu
)

set_target_properties(WorldClock PROPERTIES
    OUTPUT_NAME "WorldClock"
    VS_DPI_AWARE "PerMonitor"
)

# Embed our custom manifest (replaces CMake's default).
target_link_options(WorldClock PRIVATE
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest"
    "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
)
