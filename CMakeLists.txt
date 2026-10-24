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
# The CI sanitizer job (-DRIFTWII_SANITIZE=ON): AddressSanitizer and
# UndefinedBehaviorSanitizer on everything built here, libraries included. An
# undefined-behavior finding stops the test (no-sanitize-recover), so it fails
# the suite instead of scrolling past in the log.
# RIFTWII_SANITIZE_ADDRESS=OFF leaves AddressSanitizer out: it reserves
# address space that the low-memory suites' MAP_32BIT buffers need.
option(RIFTWII_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(RIFTWII_SANITIZE_ADDRESS "With RIFTWII_SANITIZE: include AddressSanitizer" ON)
if(RIFTWII_SANITIZE)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "RIFTWII_SANITIZE needs GCC or Clang")
    endif()
    if(RIFTWII_SANITIZE_ADDRESS)
        set(RIFTWII_SANITIZERS address,undefined)
    else()
        set(RIFTWII_SANITIZERS undefined)
    endif()
    add_compile_options(-fsanitize=${RIFTWII_SANITIZERS} -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=${RIFTWII_SANITIZERS})
endif()
find_package(ZLIB REQUIRED)
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
add_library(riftwii_runtime STATIC runtime/rtable.c runtime/rtfat.c runtime/rtfs.c runtime/rtgcad.c runtime/rtrvz.c runtime/rtshot.c runtime/rtfault.c runtime/rtvsd.c runtime/rtpro.c
    runtime/resident/rt_hook.c runtime/resident/rt_zstd.c runtime/vsd/vsd_hook.c runtime/gc/gc_rvz.c)
target_include_directories(riftwii_runtime PUBLIC runtime runtime/resident runtime/vsd PRIVATE vendor-zstd runtime/gc)
target_compile_definitions(riftwii_runtime PRIVATE RT_RVZ)
target_link_libraries(riftwii_runtime PRIVATE zstddec)
set_target_properties(riftwii_runtime PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(riftwii_runtime PRIVATE -ffreestanding -fno-builtin -Wall -Wextra -Wpedantic -Werror)
endif()
add_library(riftwii STATIC src/patching/patch.cpp src/patching/patchstream.cpp src/patching/overlay.cpp src/patching/source.cpp src/patching/apply.cpp src/patching/expand.cpp src/patching/mempatch.cpp src/patching/launch.cpp src/disc/fst.cpp src/disc/disc.cpp src/patching/redirect.cpp src/fs/fragments.cpp src/fs/fat32.cpp src/fs/ntfs.cpp src/fs/imagevolume.cpp src/fs/wbfspart.cpp src/fs/usbgame.cpp src/data/titles.cpp src/art/canvas.cpp src/disc/dol.cpp src/patching/hook.cpp src/patching/symsearch.cpp src/net/riifs.cpp src/net/riifs_sync.cpp src/net/http.cpp src/data/cheats.cpp src/patching/videopatch.cpp src/patching/gamelang.cpp src/patching/riiconfig.cpp src/net/update.cpp src/patching/wfcpatch.cpp src/data/sha1.cpp src/fs/rvz.cpp src/art/coverart.cpp src/art/pngdecode.cpp src/data/settingsfile.cpp src/data/langfile.cpp src/data/playhistory.cpp src/data/gametdb.cpp src/patching/returnto.cpp src/patching/launchargs.cpp src/data/cardlog.cpp src/patching/codehook.cpp src/patching/gameconfig.cpp src/art/pngencode.cpp src/data/shotfile.cpp src/art/qrcode.cpp src/data/problemreport.cpp src/data/gamefault.cpp src/patching/gxpatches.cpp src/patching/ctgpconfig.cpp src/art/theme.cpp src/art/skinpaint.cpp src/art/inputicons.cpp src/art/inputicons_data.cpp src/net/riitag.cpp src/disc/sysfont.cpp src/disc/brfnt.cpp src/patching/vsdparts.cpp src/patching/vsdbuild.cpp src/art/modart.cpp src/disc/bnr.cpp src/disc/bnrsound.cpp src/art/themepack.cpp src/disc/tpl.cpp src/disc/brlyt.cpp src/disc/brlan.cpp src/disc/wiicrypt.cpp src/data/d2xversion.cpp src/data/nandcheck.cpp src/data/offlinegames.cpp src/art/casecolour.cpp src/patching/extrasbudget.cpp src/patching/wpadlog.cpp src/patching/vcdiff.cpp src/patching/gcoverlay.cpp src/patching/gcrequest.cpp src/fs/gccard.cpp src/fs/gccard_text.cpp src/fs/gcbackup.cpp src/fs/gcimport.cpp src/fs/gcreal.cpp src/data/gccrash.cpp src/patching/gclayout.cpp src/patching/gcload.cpp src/patching/gcclock.cpp src/disc/gcbanner.cpp src/data/sharednand.cpp src/data/nandchannel.cpp src/data/wadfile.cpp src/data/homeorder.cpp src/data/profiles.cpp src/data/gccontrols.cpp src/patching/gcvideo.cpp src/patching/gcfont.cpp src/patching/gcdsp.cpp src/patching/gcreset.cpp src/disc/gctgc.cpp src/patching/gcexi.cpp src/patching/gcaram.cpp src/data/stashimage.cpp src/data/sha256.cpp src/data/appmeta.cpp src/data/pluginfile.cpp src/patching/osrestart.cpp src/data/textlines.cpp src/fs/zipread.cpp src/fs/zipstream.cpp src/art/themelist.cpp src/disc/gcdiscs.cpp src/data/ghcheats.cpp src/data/channelupdate.cpp)
target_include_directories(riftwii PUBLIC include PRIVATE runtime/gc)
target_link_libraries(riftwii PRIVATE pugixml zstddec ZLIB::ZLIB PUBLIC riftwii_runtime)
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
    # The production GameTDB caller and its disk cache, with no network or UI.
    add_executable(gameinfo_tests tests/gameinfo_tests.cpp wii/online/gameinfo.cpp)
    target_include_directories(gameinfo_tests PRIVATE wii/online wii/core)
    target_link_libraries(gameinfo_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gameinfo_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    # Keep sd:/ fixture paths away from other suites and the source tree.
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gameinfo-test")
    add_test(NAME gameinfo COMMAND gameinfo_tests)
    set_tests_properties(gameinfo PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gameinfo-test")
    add_executable(channel_dolboot_tests tests/channel_dolboot_tests.c)
    target_include_directories(channel_dolboot_tests PRIVATE tests/channel_mock)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # The channel deliberately casts between Wii pointers and 32-bit addresses.
        target_compile_options(channel_dolboot_tests PRIVATE -Wall -Wextra -Werror
            -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)
    endif()
    add_test(NAME channel_dolboot COMMAND channel_dolboot_tests)
    set_tests_properties(channel_dolboot PROPERTIES SKIP_RETURN_CODE 77)
    # RiiFS discovery against a clock and socket mocks, including a UDP flood.
    add_executable(netsock_tests tests/netsock_tests.cpp wii/online/netsock.cpp)
    target_include_directories(netsock_tests PRIVATE tests/netsock_mock include wii/online wii/core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(netsock_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME netsock COMMAND netsock_tests)
    # Use the plugin's actual Zstandard shim against a mocked host table,
    # without its libc replacements or a second decoder defining the API.
    add_executable(rwpzstd_tests tests/rwpzstd_tests.cpp plugins/common/rwpzstd.cpp)
    target_include_directories(rwpzstd_tests PRIVATE include plugins/common src/fs vendor-zstd)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rwpzstd_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME rwpzstd COMMAND rwpzstd_tests)
    # The physical-card adapter against EXI mocks: ownership and failure cleanup.
    add_executable(gcrealcard_io_tests tests/gcrealcard_io_tests.cpp wii/storage/gcrealcard.cpp)
    target_include_directories(gcrealcard_io_tests PRIVATE tests/gcreal_mock include wii/storage)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gcrealcard_io_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME gcrealcard_io COMMAND gcrealcard_io_tests)
    add_test(NAME gcrealcard_nomem1 COMMAND gcrealcard_io_tests no-mem1)
    # Exercise the menu's real budget calculation without requiring a Wii
    # build. These headers stand in for bin2o's generated blob declarations.
    set(memorybudget_blobs riftwii_fault_bin riftwii_pad_bin riftwii_rt_rvz_zst
        riftwii_rt_zst riftwii_shot_bin riftwii_vsd_bin)
    foreach(blob IN LISTS memorybudget_blobs)
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/memorybudget_mock/${blob}.h"
            CONTENT "#include \"memorybudget_blobs.hpp\"\n")
    endforeach()
    add_executable(memorybudget_tests tests/memorybudget_tests.cpp wii/menu/memorybudget.cpp)
    target_include_directories(memorybudget_tests PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/memorybudget_mock" tests/memorybudget_mock
        wii/menu wii/core wii/controllers wii/launch wii/storage
        runtime/resident runtime/pad runtime/shot runtime/fault runtime/vsd vendor-zstd)
    target_link_libraries(memorybudget_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(memorybudget_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME memorybudget COMMAND memorybudget_tests)
    # The Wii DI client itself, against a mocked IOS (tests/wii_mock).
    add_executable(d2xreload_tests tests/d2xreload_tests.cpp wii/launch/di.cpp)
    target_include_directories(d2xreload_tests PRIVATE tests/wii_mock wii wii/core wii/menu wii/launch wii/hooks wii/controllers wii/storage wii/online)
    target_link_libraries(d2xreload_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(d2xreload_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME d2xreload COMMAND d2xreload_tests)
    add_executable(disccache_io_tests tests/disccache_io_tests.cpp wii/launch/di.cpp)
    target_include_directories(disccache_io_tests PRIVATE tests/wii_mock wii/launch)
    target_link_libraries(disccache_io_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(disccache_io_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME disccache_io COMMAND disccache_io_tests)
    add_executable(sdio_tests tests/sdio_tests.cpp wii/storage/sdio.cpp)
    target_include_directories(sdio_tests PRIVATE tests/wii_mock wii/storage)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sdio_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME sdio COMMAND sdio_tests)
    # d2x's SD device across IOS reloads (#270), against a mocked IOS.
    add_executable(d2xsd_tests tests/d2xsd_tests.cpp wii/storage/d2xsd.cpp)
    target_include_directories(d2xsd_tests PRIVATE tests/wii_mock wii/storage)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(d2xsd_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME d2xsd COMMAND d2xsd_tests)
    # The launch's own /dev/usb/hid handle across IOS reloads (#284), against a mocked IOS.
    add_executable(hidsession_tests tests/hidsession_tests.cpp wii/hooks/hidsession.cpp)
    target_include_directories(hidsession_tests PRIVATE tests/wii_mock wii/hooks)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hidsession_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME hidsession COMMAND hidsession_tests)
    foreach(suite disccache patch overlay apply expand mempatch launch compat fst disc redirect fat32 ntfs titles canvas usbgame wbfspart rtfat rtfs dol hook riifs extras gcad png shot rvz codebuild qrcode report gamefault rtfaultstack vsd vsdblob gxpatches ctgpconfig theme riitag sysfont brfnt vsdparts vsdbuild modart bnr bnrsound themepack d2xversion nandcheck offlinegames casecolour extrasbudget wpadlog gametdb pro vcdiff gcoverlay gccard gccrash gclayout gcload gcclock gcbanner gcrvz gcports gccontrols sharednand nandchannel wadfile homeorder homeinput profiles gcvideo gcfont gcreset gctgc gcexi gcaram stash plugin gcrequest gcgcad gcdsp gcreal osrestart textlines appmeta themebrowse gcdisc ghcheats menufont zipstream channelupdate)
        add_executable(${suite}_tests tests/${suite}_tests.cpp)
        target_link_libraries(${suite}_tests PRIVATE riftwii)
        if(suite STREQUAL "zipstream")
            target_link_libraries(${suite}_tests PRIVATE ZLIB::ZLIB)
        endif()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${suite}_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        if(suite STREQUAL "gcload" OR suite STREQUAL "gccrash" OR suite STREQUAL "gcrvz" OR suite STREQUAL "gcports" OR suite STREQUAL "gccontrols" OR suite STREQUAL "gcvideo" OR suite STREQUAL "gcgcad" OR suite STREQUAL "gcexi" OR suite STREQUAL "gctgc" OR suite STREQUAL "gcdisc")
            target_include_directories(${suite}_tests PRIVATE runtime/gc)
        endif()
        # The adapter's ports in a GameCube game: its port-filling half
        # (the rest talks to IOS and is built for the Wii only).
        if(suite STREQUAL "gcgcad")
            target_sources(${suite}_tests PRIVATE runtime/gc/gc_gcad.c)
        endif()
        if(suite STREQUAL "compat" OR suite STREQUAL "rvz" OR suite STREQUAL "vcdiff" OR suite STREQUAL "stash" OR suite STREQUAL "plugin")
            add_test(NAME ${suite} COMMAND ${suite}_tests "${CMAKE_SOURCE_DIR}/tests/fixtures")
        elseif(suite STREQUAL "menufont" OR suite STREQUAL "gccrash")
            # The committed fonts/translations and the GameCube flag definitions.
            add_test(NAME ${suite} COMMAND ${suite}_tests "${CMAKE_SOURCE_DIR}")
        else()
            add_test(NAME ${suite} COMMAND ${suite}_tests)
        endif()
        # A suite that cannot run here (no memory at the Wii's addresses)
        # exits 77: shown as skipped, not passed.
        set_tests_properties(${suite} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
    add_executable(channel_entry_tests tests/channel_entry_tests.c)
    target_include_directories(channel_entry_tests PRIVATE tests/channel_mock)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # The channel deliberately casts between Wii pointers and 32-bit addresses.
        target_compile_options(channel_entry_tests PRIVATE -Wall -Wextra -Werror
            -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)
    endif()
    add_test(NAME channel_entry COMMAND channel_entry_tests)
    # The boot loader has its own validator; common/dolboot.c is tested above.
    add_executable(channel_loader_entry_tests tests/channel_loader_entry_tests.c)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(channel_loader_entry_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME channel_loader_entry COMMAND channel_loader_entry_tests)
endif()
