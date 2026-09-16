set -e

cd "/d/AI Projects/Riftwii/build-host"
/opt/devkitpro/msys2/usr/bin/cmake.exe --regenerate-during-build -S$(CMAKE_SOURCE_DIR) -B$(CMAKE_BINARY_DIR)
