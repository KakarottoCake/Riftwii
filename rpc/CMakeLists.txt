# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RiftWii-RPC: Discord rich presence on a computer for the game a Wii on the
# same network is playing in RiftWii (docs/PRESENCE.md). Qt 6, C++17.
#
#   cmake -S rpc -B build-rpc -G Ninja -DRIFTWII_RPC_CLIENT_ID=<application ID>
#   cmake --build build-rpc
#
# On macOS, -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" makes a Universal 2
# app (with a Qt that is universal itself, as Qt's own installer's is).
cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RIFTWII_RPC_VERSION LIMIT_COUNT 1)
project(RiftWii-RPC VERSION ${RIFTWII_RPC_VERSION} LANGUAGES CXX)

# The application in Discord's Developer Portal whose name Discord shows
# ("Playing RiftWii"). Empty: the player pastes one into the app's window.
set(RIFTWII_RPC_CLIENT_ID "" CACHE STRING "Discord application ID built in as the default")
option(RIFTWII_RPC_TESTS "Build RiftWii-RPC's tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network Widgets)

# Everything but the window: the Wii's messages, Discord's socket, the
# presence's rules. Shared by the app and the tests.
add_library(riftwii_rpc_core STATIC
    src/app.cpp
    src/autostart.cpp
    src/coverart.cpp
    src/discordipc.cpp
    src/listener.cpp
    src/controller.cpp
    src/settings.cpp
    # The message format, as the Wii writes it: RiftWii's own code.
    ../src/net/presence.cpp
    ../src/data/textlines.cpp)
target_include_directories(riftwii_rpc_core PUBLIC src ../include)
target_compile_definitions(riftwii_rpc_core PUBLIC
    RIFTWII_RPC_VERSION="${PROJECT_VERSION}"
    RIFTWII_RPC_CLIENT_ID="${RIFTWII_RPC_CLIENT_ID}")
target_link_libraries(riftwii_rpc_core PUBLIC Qt6::Core Qt6::Network)

set(APP_SOURCES src/main.cpp src/mainwindow.cpp src/riftwii-rpc.qrc)
if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE riftwii-rpc.icns)
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icons/riftwii-rpc.icns")
    set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND APP_SOURCES ${APP_ICON})
elseif(WIN32)
    list(APPEND APP_SOURCES src/riftwii-rpc.rc)
endif()

add_executable(RiftWii-RPC WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_link_libraries(RiftWii-RPC PRIVATE riftwii_rpc_core Qt6::Widgets)
set_target_properties(RiftWii-RPC PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER xyz.riftwii.rpc
    MACOSX_BUNDLE_BUNDLE_NAME RiftWii-RPC
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

if(UNIX AND NOT APPLE)
    install(TARGETS RiftWii-RPC RUNTIME DESTINATION bin)
    install(FILES riftwii-rpc.desktop DESTINATION share/applications)
    install(FILES icons/riftwii-rpc.png DESTINATION share/icons/hicolor/1024x1024/apps)
endif()

if(RIFTWII_RPC_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(rpc_tests tests/rpc_tests.cpp)
    target_link_libraries(rpc_tests PRIVATE riftwii_rpc_core Qt6::Test)
    add_test(NAME rpc COMMAND rpc_tests)
endif()
