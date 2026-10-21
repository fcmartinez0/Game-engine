#!/bin/sh
# The three-second picture before the game: it plays on a normal start, the game waits behind it and starts exactly when it
# ends, a press only skips it after the first second, and scripted runs (autoplay, warps, taps) don't see it.
#   intro.sh <demo_headless>
bin="$1"
tmp="$(mktemp -d)"
fail=0
sum() { (cd "$tmp" && "$bin" --no-mods --no-hints --fresh --profile ./p.txt "$@" 2>/dev/null | sed -n 's/.*checksum=\([0-9a-f]*\).*/\1/p'); }
check() { if ! eval "$2"; then echo "FAILED: $1"; fail=1; fi; }

pic=$(sum --frames 100)                                  # a normal start: the picture
game5=$(sum --no-intro --frames 5)                       # the game, five ticks in
check "a normal start shows the picture, not the game" "[ -n '$pic' ] && [ '$pic' != '$game5' ]"
check "the picture is the same however it is reached" "[ '$(sum --intro --press 'A@50' --frames 100)' = '$pic' ]"   # (a press inside the first second is ignored)
check "a press after the first second skips it" "[ '$(sum --intro --press 'A@80' --frames 100)' != '$pic' ]"
check "it is still the picture just before three seconds" "[ '$(sum --frames 178)' != '$game5' ]"
check "the game starts where it would have, when the picture ends (five ticks after it)" "[ '$(sum --frames 185)' = '$game5' ]"
check "a scripted run (a warp) goes straight to the game" "[ '$(sum --warp 20 --frames 5)' = '$(sum --warp 20 --no-intro --frames 5)' ]"
check "--autoplay does too" "[ '$(sum --autoplay --frames 5)' = '$(sum --autoplay --no-intro --frames 5)' ]"

rm -rf "$tmp"
[ $fail = 0 ] && echo "intro ok"
exit $fail
