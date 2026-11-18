cmake_minimum_required(VERSION 3.21)

project(TrayRing VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "TrayRing targets Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Self-contained executables: no VC++ redistributable / libc++ DLL needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_link_options(-static $<$<CONFIG:Release,MinSizeRel>:-s>)
endif()

# Platform-neutral logic: protocol codec, readings, charge inference, icon rasterisation.
add_library(trayring_model STATIC
    src/ronin_protocol.cpp
    src/razer_protocol.cpp
    src/bluetooth_battery.cpp
    src/battery_device.cpp
    src/charge_tracker.cpp
    src/icon_renderer.cpp)
target_include_directories(trayring_model PUBLIC src)

add_library(trayring_core STATIC
    src/hid_device.cpp
    src/ronin_keyboard.cpp
    src/razer_mouse.cpp
    src/bluetooth_headset.cpp
    src/logger.cpp)
target_link_libraries(trayring_core PUBLIC trayring_model hid cfgmgr32)

add_executable(ronin-battery-cli tools/ronin_cli.cpp)
target_link_libraries(ronin-battery-cli PRIVATE trayring_core)
if(NOT MSVC)
    target_link_options(ronin-battery-cli PRIVATE -municode)
endif()

add_executable(razer-battery-cli tools/razer_cli.cpp)
target_link_libraries(razer-battery-cli PRIVATE trayring_core)
if(NOT MSVC)
    target_link_options(razer-battery-cli PRIVATE -municode)
endif()

# Developer tool: renders all icon states to a PNG contact sheet.
add_executable(icon-preview EXCLUDE_FROM_ALL tools/icon_preview.cpp)
target_link_libraries(icon-preview PRIVATE trayring_model)

add_executable(TrayRing WIN32
    src/main.cpp
    src/tray_app.cpp
    src/tray_icon.cpp
    src/autostart.cpp
    resources/app.rc)
target_link_libraries(TrayRing PRIVATE trayring_core user32 gdi32 shell32 advapi32 ole32)
if(MSVC)
    target_sources(TrayRing PRIVATE resources/app.manifest)
else()
    target_sources(TrayRing PRIVATE resources/manifest.rc)
    target_link_options(TrayRing PRIVATE -municode)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(protocol_tests tests/protocol_tests.cpp)
    target_link_libraries(protocol_tests PRIVATE trayring_model)
    add_test(NAME protocol_tests COMMAND protocol_tests)
endif()
