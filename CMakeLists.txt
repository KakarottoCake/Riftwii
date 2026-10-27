# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
# Versions as Dolphin numbers its own: a release is the year and month it
# came out (2610: October 2026, the 2.7.0 stable release), and every build
# after it adds the number of commits since that release (2610-123 is 123
# commits on). Makefile.wii counts them from RIFTWII_BASE_TAG, the release's
# git tag. A new release: its YYMM here and its tag there (then -0 builds).
project(Riftwii VERSION 2611 LANGUAGES C CXX)
set(RIFTWII_BASE_TAG "v2610-810")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# MSYS hides usleep in strict C++17 mode unless its default POSIX/BSD
# interfaces are requested. Wii sources exercised by the host mocks use it.
if(MSYS)
    add_compile_definitions(_DEFAULT_SOURCE)
endif()
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
add_library(riftwii_runtime STATIC runtime/rtable.c runtime/rtfat.c runtime/rtfs.c runtime/rtgcad.c runtime/rthid.c runtime/rtrvz.c runtime/rtshot.c runtime/rtfault.c runtime/rtvsd.c runtime/rtvsdra.c runtime/rtpro.c runtime/rtleave.c
    runtime/resident/rt_hook.c runtime/resident/rt_zstd.c runtime/vsd/vsd_hook.c runtime/gc/gc_rvz.c)
target_include_directories(riftwii_runtime PUBLIC runtime runtime/resident runtime/vsd PRIVATE vendor-zstd runtime/gc)
target_compile_definitions(riftwii_runtime PRIVATE RT_RVZ)
target_link_libraries(riftwii_runtime PRIVATE zstddec)
set_target_properties(riftwii_runtime PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(riftwii_runtime PRIVATE -ffreestanding -fno-builtin -Wall -Wextra -Wpedantic -Werror)
endif()
add_library(riftwii STATIC src/patching/patch.cpp src/patching/patchstream.cpp src/patching/overlay.cpp src/patching/source.cpp src/patching/apply.cpp src/patching/expand.cpp src/patching/mempatch.cpp src/patching/launch.cpp src/disc/fst.cpp src/disc/disc.cpp src/patching/redirect.cpp src/fs/fragments.cpp src/fs/fat32.cpp src/fs/cardvolume.cpp src/fs/ntfs.cpp src/fs/imagevolume.cpp src/fs/wbfspart.cpp src/fs/wfs.cpp src/fs/wfstitle.cpp src/fs/usbgame.cpp src/data/titles.cpp src/art/canvas.cpp src/disc/dol.cpp src/patching/hook.cpp src/patching/symsearch.cpp src/net/riifs.cpp src/net/riifs_sync.cpp src/net/http.cpp src/data/cheats.cpp src/patching/videopatch.cpp src/patching/gamelang.cpp src/patching/riiconfig.cpp src/net/update.cpp src/patching/wfcpatch.cpp src/data/sha1.cpp src/fs/rvz.cpp src/art/coverart.cpp src/art/pngdecode.cpp src/data/settingsfile.cpp src/data/langfile.cpp src/data/playhistory.cpp src/data/gametdb.cpp src/data/homebrewmatch.cpp src/patching/returnto.cpp src/patching/launchargs.cpp src/data/cardlog.cpp src/patching/codehook.cpp src/patching/gameconfig.cpp src/art/pngencode.cpp src/data/shotfile.cpp src/art/qrcode.cpp src/data/problemreport.cpp src/data/gamefault.cpp src/patching/gxpatches.cpp src/patching/ctgpconfig.cpp src/ctgpr/ctgpapply.cpp src/ctgpr/ctgpefa.cpp src/ctgpr/ctgpfat.cpp src/ctgpr/ctgpfields.cpp src/ctgpr/ctgpfiles.cpp src/ctgpr/ctgpdsa.cpp src/ctgpr/ctgphash.cpp src/ctgpr/ctgpimage.cpp src/ctgpr/ctgplaunch.cpp src/ctgpr/ctgpmmu.cpp src/ctgpr/ctgppackages.cpp src/ctgpr/ctgpsign.cpp src/ctgpr/ctgptables.cpp src/ctgpr/ctgprowdata.cpp src/art/theme.cpp src/art/skinpaint.cpp src/art/inputicons.cpp src/art/inputicons_data.cpp src/net/riitag.cpp src/disc/sysfont.cpp src/disc/brfnt.cpp src/patching/vsdparts.cpp src/patching/vsdbuild.cpp src/art/modart.cpp src/disc/bnr.cpp src/disc/bnrsound.cpp src/art/themepack.cpp src/disc/tpl.cpp src/disc/brlyt.cpp src/disc/brlan.cpp src/disc/wiicrypt.cpp src/disc/discdump.cpp src/data/d2xversion.cpp src/data/nandcheck.cpp src/data/offlinegames.cpp src/art/casecolour.cpp src/patching/extrasbudget.cpp src/patching/wpadlog.cpp src/patching/vcdiff.cpp src/patching/gcoverlay.cpp src/patching/gcrequest.cpp src/patching/gcmempatch.cpp src/patching/gcexecpacks.cpp src/fs/gccard.cpp src/fs/gccard_text.cpp src/fs/gcbackup.cpp src/fs/gcimport.cpp src/fs/gcreal.cpp src/data/gccrash.cpp src/patching/gclayout.cpp src/patching/gcload.cpp src/patching/gcclock.cpp src/disc/gcbanner.cpp src/data/sharednand.cpp src/data/nandchannel.cpp src/data/wadfile.cpp src/data/contentbin.cpp src/data/homeorder.cpp src/data/profiles.cpp src/data/namekeys.cpp src/data/gccontrols.cpp src/patching/gcvideo.cpp src/patching/gcfont.cpp src/patching/gcdsp.cpp src/patching/gcreset.cpp src/patching/gcprograms.cpp src/disc/gctgc.cpp src/patching/gcexi.cpp src/patching/gctriforce.cpp src/patching/gcaram.cpp src/data/stashimage.cpp src/data/sha256.cpp src/data/appmeta.cpp src/data/pluginfile.cpp src/patching/osrestart.cpp src/data/textlines.cpp src/fs/zipread.cpp src/fs/zipstream.cpp src/art/themelist.cpp src/disc/gcdiscs.cpp src/data/ghcheats.cpp src/patching/returnstub.cpp src/data/channelupdate.cpp src/fs/sdhotplug.cpp src/disc/wiisound.cpp src/disc/wiisequence.cpp src/data/nandlist.cpp src/art/ratingmark.cpp src/data/parental.cpp src/data/musiclist.cpp src/data/streamformat.cpp src/data/emulatorfile.cpp src/data/wiiflowplugin.cpp src/art/emulatorart.cpp src/art/emulatorbanner.cpp src/art/picturepick.cpp src/data/hidconfig.cpp src/data/builtinpads.cpp src/data/builtinpads_data.cpp src/data/codemods.cpp src/data/modcover.cpp src/data/gamepage.cpp src/data/loaderfiles.cpp src/data/padcal.cpp src/patching/hbmsearch.cpp)
target_include_directories(riftwii PUBLIC include PRIVATE runtime/gc)
target_link_libraries(riftwii PRIVATE pugixml zstddec ZLIB::ZLIB PUBLIC riftwii_runtime)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(riftwii PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# Host tool: the IPC search on a dumped main.dol (tools/ipcscan.cpp).
add_executable(ipcscan tools/ipcscan.cpp)
target_link_libraries(ipcscan PRIVATE riftwii)
# Host timing of the in-game RVZ decoder against the vendored one (both -Os).
add_executable(rtzstd_bench tools/rtzstd_bench.cpp runtime/rtzstd.c)
target_include_directories(rtzstd_bench PRIVATE runtime vendor-zstd)
target_link_libraries(rtzstd_bench PRIVATE zstddec)

# Host tool: an RVZ's verdict and the stub d2x would boot (tools/rvzstub.cpp).
add_executable(rvzstub tools/rvzstub.cpp)
target_link_libraries(rvzstub PRIVATE riftwii)
# Host tool: the memory-card EXI finder on a GameCube ISO or RVZ (tools/gc_exi_scan.cpp).
add_executable(gc_exi_scan tools/gc_exi_scan.cpp)
target_link_libraries(gc_exi_scan PRIVATE riftwii)
# Host tool: a game's banner and icon, drawn as RiftWii draws them (tools/bannerdump.cpp).
add_executable(bannerdump tools/bannerdump.cpp)
target_link_libraries(bannerdump PRIVATE riftwii)
# Host tool: the theme kit's template pictures and starter theme.ini
# (tools/theme_kit.cpp, packed by tools/make_theme_kit.py).
add_executable(theme_kit tools/theme_kit.cpp)
target_link_libraries(theme_kit PRIVATE riftwii)
add_executable(menusound_list tools/menusound_list.cpp)
target_link_libraries(menusound_list PRIVATE riftwii)
add_executable(menusound_render tools/menusound_render.cpp)
target_link_libraries(menusound_render PRIVATE riftwii)
# Keep the timing baseline separate from the production library: both walkers
# use the runtime's size optimization even in a default host configuration.
add_executable(redirect_bench tools/redirect_bench.cpp tests/redirect_legacy.c runtime/rtable.c)
target_include_directories(redirect_bench PRIVATE runtime)
set_target_properties(redirect_bench PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(redirect_bench PRIVATE -Os -Wall -Wextra -Wpedantic -Werror)
endif()
include(CTest)
if(BUILD_TESTING)
    # Build the actual wlog C path three ways, as Makefile.runtime does.
    # Renamed entries let one test compare the same traffic and saved bytes.
    foreach(mode full trace translate)
        add_library(wlog_${mode}_host OBJECT runtime/wlog/wlog_hook.c)
        target_include_directories(wlog_${mode}_host PRIVATE runtime)
        set_target_properties(wlog_${mode}_host PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
        foreach(entry on_print on_exit on_ioctlv after_ioctlv on_bt)
            target_compile_definitions(wlog_${mode}_host PRIVATE wlog_${entry}=wlog_${mode}_${entry})
        endforeach()
        if(mode STREQUAL "trace")
            target_compile_definitions(wlog_${mode}_host PRIVATE RT_WLOG_TRACE_ONLY)
        elseif(mode STREQUAL "translate")
            target_compile_definitions(wlog_${mode}_host PRIVATE RT_WLOG_TRANSLATE_ONLY)
        endif()
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(wlog_${mode}_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endforeach()
    add_executable(wlogblob_tests tests/wlogblob_tests.cpp wii/hooks/wpadhook.cpp
        $<TARGET_OBJECTS:wlog_full_host> $<TARGET_OBJECTS:wlog_trace_host> $<TARGET_OBJECTS:wlog_translate_host>)
    target_include_directories(wlogblob_tests PRIVATE tests/wlog_mock tests/wii_mock wii/hooks wii/core wii/controllers)
    target_link_libraries(wlogblob_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wlogblob_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME wlogblob COMMAND wlogblob_tests)
    set_tests_properties(wlogblob PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(smallblob_tests tests/smallblob_tests.cpp tests/shotjob_model.c
        tests/stub_model.c tests/legacy_stub_model.c tests/fault_model.c tests/legacy_fault_model.c
        tests/legacy_shotjob_model.c tests/fixtures/small-blobs/legacy_rtshot.c
        tests/fixtures/small-blobs/legacy_rtfault.c)
    target_include_directories(smallblob_tests PRIVATE include runtime tests)
    target_link_libraries(smallblob_tests PRIVATE riftwii_runtime)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(smallblob_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME smallblob COMMAND smallblob_tests)
    set_tests_properties(smallblob PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(gccardruntime_tests tests/gccardruntime_tests.cpp)
    target_include_directories(gccardruntime_tests PRIVATE runtime runtime/gc)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gccardruntime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME gccardruntime COMMAND gccardruntime_tests)
    add_executable(spinetext_tests tests/spinetext_tests.cpp)
    target_include_directories(spinetext_tests PRIVATE wii/menu)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(spinetext_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME spinetext COMMAND spinetext_tests)
    add_executable(wadinstall_tests tests/wadinstall_tests.cpp wii/storage/wadinstall.cpp wii/storage/wadfolder.cpp)
    target_include_directories(wadinstall_tests PRIVATE
        tests/movedchannels_mock tests/wii_mock wii/storage wii/core wii/menu wii/launch runtime)
    target_link_libraries(wadinstall_tests PRIVATE riftwii)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CYGWIN OR MSYS OR MINGW)
        target_link_options(wadinstall_tests PRIVATE -Wl,--wrap=rename,--wrap=fwrite)
        target_compile_definitions(wadinstall_tests PRIVATE RIFTWII_TEST_WRAP_RENAME)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wadinstall_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wadinstall-test")
    add_test(NAME wadinstall COMMAND wadinstall_tests)
    set_tests_properties(wadinstall PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wadinstall-test")
    # The real drive-scan lifecycle, with workers and network jobs stepped by the test.
    add_executable(homescan_tests tests/homescan_tests.cpp wii/menu/homescan.cpp)
    target_include_directories(homescan_tests PRIVATE
        tests/homescan_mock tests/movedchannels_mock tests/wii_mock
        wii/menu wii/core wii/storage wii/online wii/launch runtime)
    target_link_libraries(homescan_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(homescan_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME homescan COMMAND homescan_tests)
    # Translated credits use the Wii's real wrapping code, with no stash.
    add_executable(credits_tests tests/credits_tests.cpp wii/menu/credits.cpp)
    target_include_directories(credits_tests PRIVATE tests/movedchannels_mock wii/menu wii/core)
    target_compile_definitions(credits_tests PRIVATE RIFTWII_VERSION="test")
    target_link_libraries(credits_tests PRIVATE zstddec)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(credits_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME credits COMMAND credits_tests)
    add_executable(startupnote_tests tests/startupnote_tests.cpp)
    target_link_libraries(startupnote_tests PRIVATE riftwii)
    add_test(NAME startupnote COMMAND startupnote_tests)
    add_executable(nandpermissions_tests tests/nandpermissions_tests.cpp wii/launch/nandpermissions.cpp)
    target_include_directories(nandpermissions_tests PRIVATE
        tests/installer_mock tests/movedchannels_mock tests/wii_mock wii/launch wii/core)
    find_package(Threads REQUIRED)
    target_link_libraries(nandpermissions_tests PRIVATE riftwii Threads::Threads)
    add_test(NAME nandpermissions COMMAND nandpermissions_tests)
    set_tests_properties(nandpermissions PROPERTIES SKIP_RETURN_CODE 77)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(startupnote_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(nandpermissions_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    # The SD channel cleanup itself, with NAND mocks and failed FAT renames.
    add_executable(movedchannels_tests tests/movedchannels_tests.cpp wii/storage/movedchannels.cpp)
    target_include_directories(movedchannels_tests PRIVATE
        tests/movedchannels_mock wii/storage wii/core wii/menu wii/launch)
    target_link_libraries(movedchannels_tests PRIVATE riftwii)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CYGWIN OR MSYS OR MINGW)
        target_link_options(movedchannels_tests PRIVATE -Wl,--wrap=rename)
        target_compile_definitions(movedchannels_tests PRIVATE RIFTWII_TEST_WRAP_RENAME)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(movedchannels_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/movedchannels-test")
    add_test(NAME movedchannels COMMAND movedchannels_tests)
    set_tests_properties(movedchannels PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/movedchannels-test")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Refuse each optional artwork buffer after a download, in turn.
        # MSYS replaces malloc through its C++ runtime too, which makes
        # the linker's wrapper recurse rather than simulate a refusal.
        add_executable(covermemory_tests tests/covermemory_tests.cpp)
        target_link_libraries(covermemory_tests PRIVATE riftwii)
        target_link_options(covermemory_tests PRIVATE -Wl,--wrap=malloc)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(covermemory_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME covermemory COMMAND covermemory_tests)
        set_tests_properties(covermemory PROPERTIES
            PASS_REGULAR_EXPRESSION "Art conversion handles every refused buffer")
    endif()
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
    # Exercise game deletion against an isolated card and mocked USB writes.
    add_executable(gamefiles_tests tests/gamefiles_tests.cpp wii/storage/gamefiles.cpp)
    target_include_directories(gamefiles_tests PRIVATE wii/storage wii/core wii/launch runtime)
    target_link_libraries(gamefiles_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gamefiles_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gamefiles-test")
    add_test(NAME gamefiles COMMAND gamefiles_tests)
    set_tests_properties(gamefiles PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gamefiles-test")
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
    # The in-memory HTTP client's body limits, against a fragmented response.
    add_executable(httpmem_tests tests/httpmem_tests.cpp wii/online/httpmem.cpp)
    target_include_directories(httpmem_tests PRIVATE tests/netsock_mock wii/online)
    target_link_libraries(httpmem_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(httpmem_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME httpmem COMMAND httpmem_tests)
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
        wii/menu wii/core wii/controllers wii/launch wii/storage wii/online
        runtime/resident runtime/pad runtime/shot runtime/fault runtime/vsd vendor-zstd)
    target_link_libraries(memorybudget_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(memorybudget_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME memorybudget COMMAND memorybudget_tests)
    # The adapter-only pad blob's driver (runtime/pad, wiiu#45): the
    # adapter's tests again, against rtgcad.c built without the other USB
    # pads' code. Its own copy of rtgcad.c takes the library's place.
    add_executable(gcad_adapteronly_tests tests/gcad_tests.cpp runtime/rtgcad.c)
    target_compile_definitions(gcad_adapteronly_tests PRIVATE GCAD_ADAPTER_ONLY)
    target_link_libraries(gcad_adapteronly_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gcad_adapteronly_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME gcad_adapteronly COMMAND gcad_adapteronly_tests)
    add_executable(packcatalog_tests tests/packcatalog_tests.cpp wii/menu/packcatalog.cpp)
    target_include_directories(packcatalog_tests PRIVATE wii/menu wii/core wii/launch wii/storage wii/online)
    target_link_libraries(packcatalog_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(packcatalog_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME packcatalog COMMAND packcatalog_tests)
    # The installer's temporary NAND access must be restored on every exit.
    add_executable(installer_cios_tests tests/installer_cios_tests.cpp channel/installer/cios.cpp)
    target_include_directories(installer_cios_tests PRIVATE tests/installer_mock channel/installer)
    target_link_libraries(installer_cios_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(installer_cios_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME installer_cios COMMAND installer_cios_tests)
    set_tests_properties(installer_cios PROPERTIES SKIP_RETURN_CODE 77)
    # System Menu resources through ES, with a small mock of its read calls.
    add_executable(wiimenucontent_tests tests/wiimenucontent_tests.cpp wii/menu/wiimenucontent.cpp)
    target_include_directories(wiimenucontent_tests PRIVATE tests/wii_mock wii/menu wii/core wii/launch)
    target_link_libraries(wiimenucontent_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wiimenucontent_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME wiimenucontent COMMAND wiimenucontent_tests)
    # The same contents from their NAND files, when ES refuses them.
    add_executable(wiimenunand_tests tests/wiimenunand_tests.cpp wii/menu/wiimenunand.cpp)
    target_include_directories(wiimenunand_tests PRIVATE
        tests/movedchannels_mock tests/wii_mock wii/menu wii/core wii/launch)
    target_link_libraries(wiimenunand_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wiimenunand_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME wiimenunand COMMAND wiimenunand_tests)
    # Return-to is sent before a title runs and kept until IOS reloads.
    add_executable(d2xreturnto_tests tests/d2xreturnto_tests.cpp wii/launch/d2xreturnto.cpp)
    target_include_directories(d2xreturnto_tests PRIVATE tests/wii_mock wii/launch wii/storage wii/core runtime)
    target_link_libraries(d2xreturnto_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(d2xreturnto_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME d2xreturnto COMMAND d2xreturnto_tests)
    # Channel ticket retries and the copied-NAND system launch, against ES.
    add_executable(channelidentity_tests tests/channelidentity_tests.cpp wii/launch/channelidentity.cpp)
    target_include_directories(channelidentity_tests PRIVATE tests/wii_mock wii/launch wii/core)
    target_link_libraries(channelidentity_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(channelidentity_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME channelidentity COMMAND channelidentity_tests)
    # The Wii DI client itself, against a mocked IOS (tests/wii_mock).
    add_executable(d2xreload_tests tests/d2xreload_tests.cpp wii/launch/di.cpp)
    target_include_directories(d2xreload_tests PRIVATE tests/wii_mock wii wii/core wii/menu wii/launch wii/hooks wii/controllers wii/storage wii/online)
    target_link_libraries(d2xreload_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(d2xreload_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME d2xreload COMMAND d2xreload_tests)
    add_executable(disccache_io_tests tests/disccache_io_tests.cpp wii/launch/di.cpp)
    target_include_directories(disccache_io_tests PRIVATE tests/wii_mock wii/launch wii/core)
    target_link_libraries(disccache_io_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(disccache_io_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME disccache_io COMMAND disccache_io_tests)
    # The USB keyboard's thread stopped and started while its read waits,
    # against mocked libogc threads and libwiikeyboard.
    add_executable(usbkeyboard_tests tests/usbkeyboard_tests.cpp wii/controllers/usbkeyboard.cpp)
    target_include_directories(usbkeyboard_tests PRIVATE tests/usbkeyboard_mock wii/controllers)
    find_package(Threads REQUIRED)
    target_link_libraries(usbkeyboard_tests PRIVATE Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(usbkeyboard_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME usbkeyboard COMMAND usbkeyboard_tests)
    # The loading/copying light, including timer failures and shutdown.
    add_executable(disclight_tests tests/disclight_tests.cpp wii/core/disclight.cpp)
    target_include_directories(disclight_tests PRIVATE tests/disclight_mock wii/core)
    target_link_libraries(disclight_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(disclight_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    foreach(mode normal create-failure schedule-failure)
        add_test(NAME disclight_${mode} COMMAND disclight_tests ${mode})
    endforeach()
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
    # d2x's NAND emulation from the SD card (#128): the card let go before
    # d2x's FAT module mounts it, against a mocked IOS and libfat.
    add_executable(d2xnandemu_tests tests/d2xnandemu_tests.cpp wii/storage/d2xnandemu.cpp)
    target_include_directories(d2xnandemu_tests PRIVATE tests/wii_mock wii/storage wii/core runtime)
    target_link_libraries(d2xnandemu_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(d2xnandemu_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME d2xnandemu COMMAND d2xnandemu_tests)
    # The launch's own /dev/usb/hid handle across IOS reloads (#284), against a mocked IOS.
    add_executable(hidsession_tests tests/hidsession_tests.cpp wii/hooks/hidsession.cpp)
    target_include_directories(hidsession_tests PRIVATE tests/wii_mock wii/hooks runtime)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hidsession_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME hidsession COMMAND hidsession_tests)
    # Connected mappings must get slots before absent pads fill the catalog,
    # including launches without a menu scan and with the adapter setting off.
    add_executable(hidpads_tests tests/hidpads_tests.cpp wii/controllers/hidpads.cpp wii/hooks/wpadsettings.cpp)
    target_include_directories(hidpads_tests PRIVATE tests/hidpads_mock wii/controllers wii/core wii/hooks)
    target_link_libraries(hidpads_tests PRIVATE riftwii)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hidpads_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    foreach(mode connected initialize absent unmapped adapter xbox switchpro off)
        add_test(NAME hidpads_${mode} COMMAND hidpads_tests ${mode}
            "${CMAKE_CURRENT_BINARY_DIR}/hidpads-test-${mode}")
    endforeach()
    foreach(suite disccache patch overlay apply expand mempatch launch compat fst disc redirect fat32 ntfs titles canvas usbgame wbfspart wfs wfstitle rtfat rtfs dol hook riifs extras gcad hidconfig builtinpads png shot rvz codebuild qrcode report gamefault rtfaultstack vsd vsdblob gxpatches ctgpconfig ctgparena ctgpapply ctgpefa ctgpfat ctgpfields ctgpfiles ctgpdsa ctgphash ctgpimage ctgplaunch ctgpmmu ctgppackages ctgpsign ctgptables theme riitag sysfont brfnt vsdparts vsdbuild modart bnr bnrsound themepack d2xversion nandcheck offlinegames casecolour extrasbudget wpadlog gametdb pro vcdiff gcoverlay gccard gccrash gclayout gcload gcclock gcbanner gcrvz gcports gccontrols sharednand nandchannel wadfile contentbin homeorder homeinput profiles namekeys gcvideo gcfont gcreset gcprograms gctgc gcexi gctri gcvolume gcaram stash plugin gcrequest gcmempatch gcexecpacks gcgcad gcdsp gcreal osrestart textlines appmeta homebrewmatch themebrowse gcdisc ghcheats menufont zipstream cardvolume returnstub padleave channelupdate sdhotplug wiisound nandlist ratingmark parental musiclist menumotion streamformat emulatorfile emulatorart emulatorbanner picturepick wiiflowplugin codemods modcover homechord discdump gamepage rtzstd vsdra gcstorage padcal hbmsearch)
        add_executable(${suite}_tests tests/${suite}_tests.cpp)
        target_link_libraries(${suite}_tests PRIVATE riftwii)
        if(suite STREQUAL "redirect")
            target_sources(${suite}_tests PRIVATE tests/redirect_legacy.c)
        endif()
        if(suite STREQUAL "vsd")
            target_sources(${suite}_tests PRIVATE tests/vsd_legacy.c)
        endif()
        if(suite STREQUAL "zipstream")
            target_link_libraries(${suite}_tests PRIVATE ZLIB::ZLIB)
        endif()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${suite}_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        if(suite STREQUAL "gcload" OR suite STREQUAL "gccrash" OR suite STREQUAL "gcrvz" OR suite STREQUAL "gcports" OR suite STREQUAL "gccontrols" OR suite STREQUAL "gcvideo" OR suite STREQUAL "gcgcad" OR suite STREQUAL "gcexi" OR suite STREQUAL "gctri" OR suite STREQUAL "gcvolume" OR suite STREQUAL "gctgc" OR suite STREQUAL "gcdisc" OR suite STREQUAL "gcprograms" OR suite STREQUAL "gcstorage")
            target_include_directories(${suite}_tests PRIVATE runtime/gc)
        endif()
        # The adapter's ports in a GameCube game: its port-filling half
        # (the rest talks to IOS and is built for the Wii only).
        if(suite STREQUAL "gcgcad")
            target_sources(${suite}_tests PRIVATE runtime/gc/gc_gcad.c)
        endif()
        # The Triforce boards' platform-free half (runtime/gc/gc_tri.c).
        if(suite STREQUAL "gctri")
            target_sources(${suite}_tests PRIVATE runtime/gc/gc_tri.c)
        endif()
        if(suite STREQUAL "rtzstd")
            # RiftWii's in-game decoder against the vendored reference one.
            target_sources(${suite}_tests PRIVATE tests/rtzstd_probe.c)
            target_include_directories(${suite}_tests PRIVATE runtime vendor-zstd)
            target_link_libraries(${suite}_tests PRIVATE zstddec)
            add_test(NAME ${suite} COMMAND ${suite}_tests "${CMAKE_SOURCE_DIR}/tests/fixtures")
        elseif(suite STREQUAL "compat" OR suite STREQUAL "rvz" OR suite STREQUAL "vcdiff" OR suite STREQUAL "stash" OR suite STREQUAL "plugin")
            add_test(NAME ${suite} COMMAND ${suite}_tests "${CMAKE_SOURCE_DIR}/tests/fixtures")
        elseif(suite STREQUAL "vsd")
            # The blob's entries are checked too when the Wii build has made it.
            add_test(NAME ${suite} COMMAND ${suite}_tests "${CMAKE_SOURCE_DIR}/build-runtime/riftwii_vsd.bin")
        elseif(suite STREQUAL "menufont" OR suite STREQUAL "gccrash" OR suite STREQUAL "ratingmark" OR suite STREQUAL "codebuild")
            # The committed fonts/translations, the GameCube flag definitions,
            # the rating marks and the Gecko code handlers (vendor-gecko).
            add_test(NAME ${suite} COMMAND ${suite}_tests "${CMAKE_SOURCE_DIR}")
        else()
            add_test(NAME ${suite} COMMAND ${suite}_tests)
        endif()
        # A suite that cannot run here (no memory at the Wii's addresses)
        # exits 77: shown as skipped, not passed.
        set_tests_properties(${suite} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
    # Keep the pre-rewrite engine as a host-only differential oracle. Both
    # executables use the same scenarios and emit address-free I/O transcripts.
    add_library(fs_reference STATIC tests/reference/rtfat.c tests/reference/rtfs.c)
    target_include_directories(fs_reference PUBLIC tests/reference)
    set_target_properties(fs_reference PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    foreach(suite rtfat rtfs)
        add_executable(${suite}_reference_tests tests/${suite}_tests.cpp)
        target_compile_definitions(${suite}_reference_tests PRIVATE RTFAT_REFERENCE)
        target_include_directories(${suite}_reference_tests BEFORE PRIVATE tests/reference)
        target_link_libraries(${suite}_reference_tests PRIVATE fs_reference riftwii)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${suite}_reference_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME ${suite}_differential COMMAND "${CMAKE_COMMAND}"
            "-Dreference=$<TARGET_FILE:${suite}_reference_tests>"
            "-Dcurrent=$<TARGET_FILE:${suite}_tests>"
            "-Dtrace=${CMAKE_CURRENT_BINARY_DIR}/${suite}-trace"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/fs_differential.cmake")
    endforeach()
    add_executable(channel_entry_tests tests/channel_entry_tests.c)
    target_include_directories(channel_entry_tests PRIVATE tests/channel_mock)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # The channel deliberately casts between Wii pointers and 32-bit addresses.
        target_compile_options(channel_entry_tests PRIVATE -Wall -Wextra -Werror
            -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)
    endif()
    add_test(NAME channel_entry COMMAND channel_entry_tests)
    # The far copy that starts a DOL landing on the running program.
    add_executable(channel_farcopy_tests tests/channel_farcopy_tests.c)
    target_include_directories(channel_farcopy_tests PRIVATE tests/channel_mock)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(channel_farcopy_tests PRIVATE -Wall -Wextra -Werror
            -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)
    endif()
    add_test(NAME channel_farcopy COMMAND channel_farcopy_tests "${CMAKE_SOURCE_DIR}/tests/fixtures")
    # The boot loader has its own validator; common/dolboot.c is tested above.
    add_executable(channel_loader_entry_tests tests/channel_loader_entry_tests.c)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(channel_loader_entry_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME channel_loader_entry COMMAND channel_loader_entry_tests)
endif()
