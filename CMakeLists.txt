# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
# Versions as Dolphin numbers its own: a release is the year and month it
# came out (2610: October 2026, the 2.7.0 stable release), and every build
# after it adds the number of commits since that release (2610-123 is 123
# commits on). Makefile.wii counts them from RIFTWII_BASE_TAG, the release's
# git tag. A new release: its YYMM here and its tag there (then -0 builds).
project(Riftwii VERSION 2610 LANGUAGES C CXX)
set(RIFTWII_BASE_TAG "v2.7.0")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(pugixml STATIC vendor-pugixml/src/pugixml.cpp)
target_include_directories(pugixml PUBLIC vendor-pugixml/src)
# Zstandard's decoder only (zstddeclib.c, built from zstd 1.5.7's
# build/single_file_libs), for RVZ images.
add_library(zstddec STATIC vendor-zstd/zstddeclib.c)
target_include_directories(zstddec PUBLIC vendor-zstd)
set_target_properties(zstddec PROPERTIES C_STANDARD 99)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zstddec PRIVATE -w)
endif()
# Freestanding runtime pieces: compiled for the host here so their logic is
# unit-tested, and for PowerPC by the Wii build. No libc, no allocation.
# The resident runtime is built with its RVZ code (RT_RVZ): the tests cover
# both blobs' logic, and without an RVZ state the RVZ code stays out of the
# way. Its Zstandard is the host library here.
add_library(riftwii_runtime STATIC runtime/rtable.c runtime/rtfat.c runtime/rtfs.c runtime/rtgcad.c runtime/rtrvz.c runtime/rtshot.c runtime/rtfault.c runtime/rtvsd.c
    runtime/resident/rt_hook.c runtime/resident/rt_zstd.c runtime/vsd/vsd_hook.c)
target_include_directories(riftwii_runtime PUBLIC runtime runtime/resident runtime/vsd PRIVATE vendor-zstd)
target_compile_definitions(riftwii_runtime PRIVATE RT_RVZ)
target_link_libraries(riftwii_runtime PRIVATE zstddec)
set_target_properties(riftwii_runtime PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(riftwii_runtime PRIVATE -ffreestanding -fno-builtin -Wall -Wextra -Wpedantic -Werror)
endif()
add_library(riftwii STATIC src/patching/patch.cpp src/patching/overlay.cpp src/patching/source.cpp src/patching/apply.cpp src/patching/expand.cpp src/patching/mempatch.cpp src/patching/launch.cpp src/disc/fst.cpp src/disc/disc.cpp src/patching/redirect.cpp src/fs/fat32.cpp src/fs/ntfs.cpp src/fs/imagevolume.cpp src/fs/wbfspart.cpp src/fs/usbgame.cpp src/data/titles.cpp src/art/canvas.cpp src/disc/dol.cpp src/patching/hook.cpp src/patching/symsearch.cpp src/net/riifs.cpp src/net/riifs_sync.cpp src/net/http.cpp src/data/cheats.cpp src/patching/videopatch.cpp src/patching/gamelang.cpp src/patching/riiconfig.cpp src/net/update.cpp src/patching/wfcpatch.cpp src/data/sha1.cpp src/fs/rvz.cpp src/art/coverart.cpp src/art/pngdecode.cpp src/data/settingsfile.cpp src/data/langfile.cpp src/data/playhistory.cpp src/patching/returnto.cpp src/patching/launchargs.cpp src/data/cardlog.cpp src/patching/codehook.cpp src/patching/gameconfig.cpp src/art/pngencode.cpp src/data/shotfile.cpp src/art/qrcode.cpp src/data/problemreport.cpp src/data/gamefault.cpp src/patching/gxpatches.cpp src/patching/ctgpconfig.cpp src/art/theme.cpp src/art/skinpaint.cpp src/net/riitag.cpp src/disc/sysfont.cpp src/disc/brfnt.cpp src/patching/vsdparts.cpp src/patching/vsdbuild.cpp src/art/modart.cpp src/disc/bnr.cpp src/disc/bnrsound.cpp src/art/themepack.cpp src/disc/tpl.cpp src/disc/brlyt.cpp src/disc/brlan.cpp src/disc/wiicrypt.cpp src/data/d2xversion.cpp src/data/nandcheck.cpp src/data/offlinegames.cpp src/art/casecolour.cpp src/patching/extrasbudget.cpp)
target_include_directories(riftwii PUBLIC include)
target_link_libraries(riftwii PRIVATE pugixml zstddec PUBLIC riftwii_runtime)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(riftwii PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# Host tool: the IPC search on a dumped main.dol (tools/ipcscan.cpp).
add_executable(ipcscan tools/ipcscan.cpp)
target_link_libraries(ipcscan PRIVATE riftwii)
# Host tool: an RVZ's verdict and the stub d2x would boot (tools/rvzstub.cpp).
add_executable(rvzstub tools/rvzstub.cpp)
target_link_libraries(rvzstub PRIVATE riftwii)
# Host tool: a game's banner and icon, drawn as RiftWii draws them (tools/bannerdump.cpp).
add_executable(bannerdump tools/bannerdump.cpp)
target_link_libraries(bannerdump PRIVATE riftwii)
# Host tool: the theme kit's template pictures and starter theme.ini
# (tools/theme_kit.cpp, packed by tools/make_theme_kit.py).
add_executable(theme_kit tools/theme_kit.cpp)
target_link_libraries(theme_kit PRIVATE riftwii)
include(CTest)
if(BUILD_TESTING)
    # The Wii DI client itself, against a mocked IOS (tests/wii_mock).
    add_executable(d2xreload_tests tests/d2xreload_tests.cpp wii/launch/di.cpp)
    target_include_directories(d2xreload_tests PRIVATE tests/wii_mock wii wii/core wii/menu wii/launch wii/hooks wii/controllers wii/storage wii/online)
    target_link_libraries(d2xreload_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(d2xreload_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME d2xreload COMMAND d2xreload_tests)
    foreach(suite patch overlay apply expand mempatch launch compat fst disc redirect fat32 ntfs titles canvas usbgame wbfspart rtfat rtfs dol hook riifs extras gcad png shot rvz codebuild qrcode report gamefault vsd vsdblob gxpatches ctgpconfig theme riitag sysfont brfnt vsdparts vsdbuild modart bnr bnrsound themepack d2xversion nandcheck offlinegames casecolour extrasbudget)
        add_executable(${suite}_tests tests/${suite}_tests.cpp)
        target_link_libraries(${suite}_tests PRIVATE riftwii)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${suite}_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        if(suite STREQUAL "compat" OR suite STREQUAL "rvz")
            add_test(NAME ${suite} COMMAND ${suite}_tests "${CMAKE_SOURCE_DIR}/tests/fixtures")
        else()
            add_test(NAME ${suite} COMMAND ${suite}_tests)
        endif()
        # A suite that cannot run here (no memory at the Wii's addresses)
        # exits 77: shown as skipped, not passed.
        set_tests_properties(${suite} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
endif()
