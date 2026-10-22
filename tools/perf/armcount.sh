#!/bin/sh
# ARM instructions per frame of the game's drawing, the way the original 3DS's ARM11 would run them: the
# headless demo is built with the 3DS compiler flags (-O3 -march=armv6k -mtune=mpcore, VFP, no NEON) for
# 32-bit ARM Linux, run under qemu-arm with its block trace on, and tools/perf/qcount.c counts what runs
# inside demo_draw. Instruction counts on a desktop CPU mislead here: the ARM11 runs about twice as many
# for the same C, and very differently for some of it (division is a library call).
# Needs arm-linux-gnueabihf-gcc and qemu-arm (Debian/Ubuntu: gcc-arm-linux-gnueabihf qemu-user).
#   usage: tools/perf/armcount.sh <first-frame> <frames> [demo args...]
#   e.g.   tools/perf/armcount.sh 300 4 --low-detail --autoplay --god --warp 80     (mid-duel)
#   PER=1 in the environment also prints each frame (for spikes). Takes a minute or two: qemu traces it all.
set -e
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${ARM_BUILD:-$root/build-arm}"
first="$1"; n="$2"; shift 2
cmake -S "$root" -B "$out" -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG" \
  -DCMAKE_C_FLAGS="-march=armv6k -mtune=mpcore -marm -mfpu=vfp -mfloat-abi=hard -fomit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-static" -DCMAKE_C_STANDARD_LIBRARIES="-lm" >/dev/null
cmake --build "$out" --target demo_headless >/dev/null
cc -O2 -o "$out/qcount" "$root/tools/perf/qcount.c"
bin="$out/demo_headless"
start=$(arm-linux-gnueabihf-nm "$bin" | awk '$3=="demo_draw"{print $1}')
stop=$(arm-linux-gnueabihf-nm "$bin" | awk '$3=="draw_bottom"{print $1}')
tmp="$(mktemp -d)"
( cd "$tmp" && qemu-arm -d nochain,exec,in_asm "$bin" --no-mods --no-hints --fresh --profile "$tmp/p.txt" \
    --frames $((first + n + 1)) "$@" 2>&1 >/dev/null | "$out/qcount" "$start" "$stop" "$first" "$n" )
rm -rf "$tmp"
