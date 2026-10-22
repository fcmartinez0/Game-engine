#!/bin/sh
# Build gloamwood.exe for 64-bit Windows with MinGW and pack dist/gloamwood-windows.zip.
#   tools/package-windows.sh <path to SDL2's x86_64-w64-mingw32 folder>
# Needs gcc-mingw-w64-x86-64 (apt install gcc-mingw-w64-x86-64) and cmake. SDL2's MinGW development package
# (SDL2-devel-2.x-mingw.tar.gz from https://github.com/libsdl-org/SDL/releases) unpacks to a folder containing
# x86_64-w64-mingw32/: pass that one.
set -e
sdl="$1"
[ -d "$sdl/lib/cmake/SDL2" ] || { echo "usage: $0 <SDL2 x86_64-w64-mingw32 folder>"; exit 1; }
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build-win"
tc="$(mktemp)"
cat > "$tc" <<TC
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
TC
cmake -S "$root" -B "$out" -DCMAKE_TOOLCHAIN_FILE="$tc" -DCMAKE_BUILD_TYPE=Release \
      -DSDL2_DIR="$sdl/lib/cmake/SDL2" -DCMAKE_PREFIX_PATH="$sdl" > /dev/null
cmake --build "$out" -j --target demo
rm -f "$tc"
x86_64-w64-mingw32-strip "$out/demo.exe"
rm -rf "$root/dist/win" "$root/dist/gloamwood-windows.zip"
mkdir -p "$root/dist/win/gloamwood"
cp "$out/demo.exe" "$root/dist/win/gloamwood/gloamwood.exe"
cp "$sdl/bin/SDL2.dll" "$root/dist/win/gloamwood/"
cp "$root/pc/README-PC.txt" "$root/dist/win/gloamwood/README.txt"
cp "$root/README.md" "$root/dist/win/gloamwood/README-full.md"
(cd "$root/dist/win" && python3 -m zipfile -c ../gloamwood-windows.zip gloamwood)
echo "dist/gloamwood-windows.zip ready"
