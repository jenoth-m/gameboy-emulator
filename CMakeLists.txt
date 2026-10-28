cmake_minimum_required(VERSION 3.20)
project(GameBoyEmulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The same core is used by the window, command-line runner and tests.
add_library(gb_core
    src/CPU.cpp
    src/Bus.cpp
    src/Cartridge.cpp
    src/PPU.cpp
)

target_include_directories(gb_core PUBLIC src)

if(MSVC)
    target_compile_options(gb_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(gb_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gb_headless src/headless.cpp)
target_link_libraries(gb_headless PRIVATE gb_core)

add_executable(gb_tests tests/tests.cpp)
target_link_libraries(gb_tests PRIVATE gb_core)

add_executable(gb_games_tests tests/games_tests.cpp)
target_link_libraries(gb_games_tests PRIVATE gb_core)

enable_testing()
add_test(NAME core COMMAND gb_tests)
add_test(NAME games COMMAND gb_games_tests "${CMAKE_CURRENT_SOURCE_DIR}/roms")

if(WIN32)
    add_executable(GameBoyEmulator WIN32 src/windows_main.cpp)
    target_compile_definitions(GameBoyEmulator PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(GameBoyEmulator PRIVATE gb_core user32 gdi32 comdlg32)

    # MinGW needs an explicit Unicode entry-point option.
    if(MINGW)
        target_link_options(GameBoyEmulator PRIVATE -municode)
    endif()

    add_custom_command(TARGET GameBoyEmulator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/roms"
            "$<TARGET_FILE_DIR:GameBoyEmulator>/roms"
    )

    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT GameBoyEmulator)
endif()
