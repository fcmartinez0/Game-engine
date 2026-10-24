#!/bin/sh
# Build "Fran's demo.app" for macOS (Apple Silicon and Intel in the one app) and pack dist/frans-demo-mac.zip.
#   tools/package-mac.sh [folder where a static SDL2 is installed]
# Run it on a Mac that has Xcode's command line tools and CMake (xcode-select --install; brew install cmake).
# With no argument it downloads the pinned SDL2 release (checked against its SHA-256) and builds it once into
# build-mac/, so the app carries SDL2 inside itself and needs nothing installed on the player's Mac.
# The app is signed "ad hoc" (no Apple developer account): it runs, but macOS asks the person to approve it the
# first time (pc/README-MAC.txt says how).
#   MAC_ARCHS="arm64"   builds for this kind of Mac only (faster; the default is "arm64;x86_64")
set -eu

SDL_VERSION=2.32.10
SDL_SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165

[ "$(uname -s)" = Darwin ] || { echo "$0 builds the Mac app: run it on a Mac" >&2; exit 1; }
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$root/build-mac"
archs="${MAC_ARCHS:-arm64;x86_64}"
target="${MAC_TARGET:-11.0}"
version="${MAC_VERSION:-1.0}"
build="$(git -C "$root" rev-list --count HEAD 2>/dev/null || echo 1)"
jobs="$(sysctl -n hw.ncpu)"
name="Fran's demo"
exe="frans-demo"
tag="$(echo "$archs" | tr ';' '-')"

sdl="${1:-}"
if [ -z "$sdl" ]; then
    sdl="$work/sdl2-$SDL_VERSION-$tag"
    if [ ! -f "$sdl/lib/libSDL2.a" ]; then
        mkdir -p "$work"
        tarball="$work/SDL2-$SDL_VERSION.tar.gz"
        [ -f "$tarball" ] || curl -fsSL -o "$tarball" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz"
        got="$(shasum -a 256 "$tarball" | cut -d' ' -f1)"
        if [ "$got" != "$SDL_SHA256" ]; then
            rm -f "$tarball"
            echo "the SDL2 download has the wrong checksum ($got): not using it" >&2
            exit 1
        fi
        rm -rf "$work/sdl2-src-$tag" "$work/sdl2-build-$tag"
        mkdir -p "$work/sdl2-src-$tag"
        tar -xzf "$tarball" -C "$work/sdl2-src-$tag" --strip-components=1
        echo "building SDL2 $SDL_VERSION ($archs), once..."
        cmake -S "$work/sdl2-src-$tag" -B "$work/sdl2-build-$tag" -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_OSX_ARCHITECTURES="$archs" -DCMAKE_OSX_DEPLOYMENT_TARGET="$target" \
              -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$sdl" > /dev/null
        cmake --build "$work/sdl2-build-$tag" -j "$jobs" > "$work/sdl2-build-$tag.log" 2>&1 || { tail -40 "$work/sdl2-build-$tag.log" >&2; exit 1; }
        cmake --install "$work/sdl2-build-$tag" > /dev/null
    fi
fi
[ -d "$sdl/lib/cmake/SDL2" ] || { echo "no SDL2 in $sdl (expected lib/cmake/SDL2 inside it)" >&2; exit 1; }

echo "building the game ($archs)..."
cmake -S "$root" -B "$work/game-$tag" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$archs" -DCMAKE_OSX_DEPLOYMENT_TARGET="$target" \
      -DCMAKE_PREFIX_PATH="$sdl" -DSDL2_DIR="$sdl/lib/cmake/SDL2" > /dev/null
cmake --build "$work/game-$tag" -j "$jobs" --target demo

stage="$root/dist/mac/stage"
app="$stage/$name.app"
rm -rf "$root/dist/mac" "$root/dist/frans-demo-mac.zip"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$work/game-$tag/demo" "$app/Contents/MacOS/$exe"
strip -x "$app/Contents/MacOS/$exe"
cp "$root/pc/mac/icon.icns" "$app/Contents/Resources/icon.icns"
sed -e "s/@VERSION@/$version/" -e "s/@BUILD@/$build/" "$root/pc/mac/Info.plist.in" > "$app/Contents/Info.plist"
printf 'APPL????' > "$app/Contents/PkgInfo"
plutil -lint "$app/Contents/Info.plist"

# the program may use the system's own libraries and nothing else (a Homebrew SDL2 would not exist on the player's Mac)
stray="$(otool -L "$app/Contents/MacOS/$exe" | grep '^[[:space:]]' | awk '{print $1}' | grep -v -E '^(/usr/lib/|/System/Library/)' || true)"
if [ -n "$stray" ]; then
    echo "the program is linked to libraries that other Macs don't have:" >&2
    echo "$stray" >&2
    exit 1
fi

# sign last: nothing in the app may change after this
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

cp "$root/pc/README-MAC.txt" "$stage/READ ME FIRST.txt"
(cd "$stage" && ditto -c -k --sequesterRsrc . "$root/dist/frans-demo-mac.zip")
echo "dist/frans-demo-mac.zip ready: $(lipo -archs "$app/Contents/MacOS/$exe"), $(du -h "$root/dist/frans-demo-mac.zip" | cut -f1)"
