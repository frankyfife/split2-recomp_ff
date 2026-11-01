# split2 launcher: the native settings window for TimeSplitters 2, in Qt 6
# Widgets. It replaces the toolkit's drawn launcher (split2_recomp_launcher)
# on the desktop and edits the same files, so either can be used:
#
#   %APPDATA%\xboxrecomp\titles\4553000A.conf   (the game's settings)
#   %APPDATA%\xboxrecomp\input_bindings.json    (the controls, every game)
#
# Unlike the toolkit's, it keeps every line it does not set (keys the game
# reads that have no row, comments a player wrote), shows TimeSplitters 2's
# own options and the mouse aim, and starts the game with the switches that
# have no settings key (renderer, mute, mods) in its environment.
#
# Standalone (any Qt 6.4+ kit):
#   cmake -S launcher -B build/launcher -DCMAKE_PREFIX_PATH=<Qt>/lib/cmake
#   cmake --build build/launcher --config Release
# or from the top-level project with -DSPLIT2_QT_LAUNCHER=ON. The executable
# goes beside split2_recomp(.exe). On Windows, windeployqt puts the Qt DLLs
# beside it: windeployqt --release --no-translations split2_launcher.exe
cmake_minimum_required(VERSION 3.21)
project(Split2Launcher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(split2_launcher WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.cpp src/mainwindow.h
    src/conffile.cpp src/conffile.h
    src/bindings.cpp src/bindings.h
    src/paths.cpp src/paths.h
    src/builder.cpp src/builder.h
    src/tr.cpp src/tr.h)
target_compile_definitions(split2_launcher PRIVATE
    SPLIT2_LAUNCHER_VERSION="${PROJECT_VERSION}"
)
if(MSVC)
    target_compile_options(split2_launcher PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(split2_launcher PRIVATE UNICODE _UNICODE NOMINMAX)
else()
    target_compile_options(split2_launcher PRIVATE -Wall -Wextra)
endif()
target_link_libraries(split2_launcher PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# Offline checks of the file handling (no window, no game): ctest.
include(CTest)
if(BUILD_TESTING)
    add_executable(split2_launcher_test tests/test_files.cpp
        src/conffile.cpp src/bindings.cpp src/paths.cpp)
    target_include_directories(split2_launcher_test PRIVATE src)
    target_link_libraries(split2_launcher_test PRIVATE Qt6::Core)
    add_test(NAME split2_launcher_files COMMAND split2_launcher_test)
endif()
