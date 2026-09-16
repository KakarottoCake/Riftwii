set -e

cd "/d/AI Projects/Riftwii/build-host"
/opt/devkitpro/msys2/usr/bin/ctest.exe -D NightlyCoverage
