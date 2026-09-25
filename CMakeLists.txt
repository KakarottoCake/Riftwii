cmake_minimum_required(VERSION 3.20)
project(Riftwii VERSION 2.0.5 LANGUAGES C CXX)
# Shown after the version number (menu, logs): "" for a final release.
set(RIFTWII_VERSION_SUFFIX "-beta")
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
add_library(riftwii_runtime STATIC runtime/rtable.c runtime/rtfat.c runtime/rtfs.c runtime/rtgcad.c runtime/rtrvz.c
    runtime/resident/rt_hook.c runtime/resident/rt_zstd.c)
target_include_directories(riftwii_runtime PUBLIC runtime runtime/resident PRIVATE vendor-zstd)
target_compile_definitions(riftwii_runtime PRIVATE RT_RVZ)
target_link_libraries(riftwii_runtime PRIVATE zstddec)
set_target_properties(riftwii_runtime PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(riftwii_runtime PRIVATE -ffreestanding -fno-builtin -Wall -Wextra -Wpedantic -Werror)
endif()
add_library(riftwii STATIC src/patch.cpp src/overlay.cpp src/source.cpp src/apply.cpp src/expand.cpp src/mempatch.cpp src/launch.cpp src/fst.cpp src/disc.cpp src/redirect.cpp src/fat32.cpp src/ntfs.cpp src/imagevolume.cpp src/usbgame.cpp src/titles.cpp src/canvas.cpp src/dol.cpp src/hook.cpp src/symsearch.cpp src/riifs.cpp src/riifs_sync.cpp src/http.cpp src/cheats.cpp src/videopatch.cpp src/gamelang.cpp src/riiconfig.cpp src/update.cpp src/wfcpatch.cpp src/sha1.cpp src/rvz.cpp src/coverart.cpp src/pngdecode.cpp src/settingsfile.cpp src/langfile.cpp src/playhistory.cpp src/cardlog.cpp)
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
include(CTest)
if(BUILD_TESTING)
    foreach(suite patch overlay apply expand mempatch launch compat fst disc redirect fat32 ntfs titles canvas usbgame rtfat rtfs dol hook riifs extras gcad png rvz)
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
    endforeach()
endif()
