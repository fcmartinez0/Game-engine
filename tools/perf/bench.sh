#!/bin/sh
# Frame-cost benchmark used for the 60 fps work. Builds the headless demo, then for each scene measures
# the drawing code only (callgrind, collection switched on inside demo_draw) with a cache shaped like the
# original 3DS's ARM11: 16 KB 4-way L1 data cache, 32-byte lines, and no L2 (LL set equal to L1, so an LL
# miss means a trip to main memory). Prints per-frame instructions, main-memory reads/writes, an estimated
# cycle cost, and the last frame's checksum (pixel-exact optimisations must leave it unchanged).
#   usage: tools/perf/bench.sh [build-dir]        (default: build-bench, created from the current tree)
set -e
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:-$root/build-bench}"
cmake -S "$root" -B "$out" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-O2 -g" >/dev/null
cmake --build "$out" --target demo_headless >/dev/null
bin="$out/demo_headless"
tmp="$(mktemp -d)"
FR=${FRAMES:-240}
scene() {   # name, args...
  name="$1"; shift
  ( cd "$tmp" && valgrind --tool=callgrind --toggle-collect=demo_draw --cache-sim=yes \
      --D1=16384,4,32 --I1=16384,4,32 --LL=16384,4,32 --callgrind-out-file="$tmp/$name.cg" \
      "$bin" --no-mods --no-hints --fresh --profile "$tmp/p.txt" --frames "$FR" "$@" > "$tmp/$name.out" 2>/dev/null )
  sum=$(callgrind_annotate "$tmp/$name.cg" 2>/dev/null | grep "PROGRAM TOTALS" | head -1)
  ck=$(sed -n 's/.*checksum=\([0-9a-f]*\).*/\1/p' "$tmp/$name.out")
  echo "$sum" | awk -v n="$name" -v fr="$FR" -v ck="$ck" '{
    gsub(",","",$0); k=0;
    for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+$/) a[++k] = $i;
    # callgrind order: Ir Dr Dw I1mr D1mr D1mw ILmr DLmr DLmw
    ir=a[1]; dlmr=a[8]; dlmw=a[9];
    cyc = ir + 40*dlmr + 12*dlmw;
    printf "%-10s Ir/frame %8.0f  mem reads %6.0f  mem writes %6.0f  est cycles %8.0f  checksum %s\n", n, ir/fr, dlmr/fr, dlmw/fr, cyc/fr, ck }'
}
scene forest   --low-detail --warp 20
scene forest2  --low-detail --god --warp 50
scene full     --warp 20
scene duel     --low-detail --autoplay --god --warp 80
scene hall     --low-detail --warp 186 --difficulty -1
scene paragon  --low-detail --autoplay --god --paragon --difficulty -1
rm -rf "$tmp"
