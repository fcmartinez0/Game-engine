#!/bin/sh
# Thornwarden through the real game loop (demo/main.c), headless: the things only the whole game can get wrong.
#   boss_flow.sh <demo_headless>
bin="$1"
tmp="$(mktemp -d)"
fail=0
run() {   # run <name> <args...>: plays in its own folder, leaves gloamwood_log.txt and last.txt there
    name="$1"; shift
    mkdir -p "$tmp/$name"
    (cd "$tmp/$name" && "$bin" --no-mods --no-hints --fresh --profile ./p.txt --log "$@" 2>/dev/null | tail -1 > last.txt)
}
check() { if ! eval "$2"; then echo "FAILED: $1"; fail=1; fi; }

# 1. A needle thrown from the doorway, before the hero has set foot in the arena, wakes it (it used to be a punching bag
#    that was killed without ever fighting back).
taps=$(awk 'BEGIN { for (i = 0; i < 40; i++) printf "%sX@%d", (i ? "," : ""), 60 + i * 100 }')
run needle --warp 156 --difficulty 0 --frames 5000 --press "$taps"
check "a needle from the doorway wakes the boss" "grep -q 'boss engaged' $tmp/needle/gloamwood_log.txt"

# 2. After the boss is gone its entity slot is free and the hero's respawn takes it: switching the difficulty afterwards
#    must not give the hero the boss's health (it used to leave 200 masks).
run slot --autoplay --god --warp 152 --difficulty 0 --frames 6600 --press 'SELECT@6300,DOWN@6302,DOWN@6304,DOWN@6306,DOWN@6308,RIGHT@6310,SELECT@6312'
check "the hero keeps hard's four masks after the boss and the Paragon (got: $(cat $tmp/slot/last.txt | sed 's/.*masks=\([0-9]*\).*/\1/'))" "grep -q 'masks=4 ' $tmp/slot/last.txt"

# 3. Warping out of the arena mid-fight ends the fight, forward as well as back.
run warp --god --warp 158 --difficulty 0 --frames 1500 --press 'SELECT@120,DOWN@122,DOWN@124,DOWN@126,DOWN@128,DOWN@130,RIGHT@132,RIGHT@134,RIGHT@136,RIGHT@138,RIGHT@140,RIGHT@142,RIGHT@144,RIGHT@146,RIGHT@148,RIGHT@150,RIGHT@152,A@160'
check "a warp past the arena ends the fight (got: $(sed 's/.*boss=\([a-z]*\).*/\1/' $tmp/warp/last.txt))" "grep -q 'boss=waiting' $tmp/warp/last.txt"

# 4. A pit in the arena floor (rooms people build can have one): the boss does not fall into it, however long the fight.
"$bin" --no-mods --export-world "$tmp/world.txt" 2>/dev/null
awk 'NR >= 21 && NR <= 23 { $0 = substr($0, 1, 163) ".." substr($0, 166) } { print }' "$tmp/world.txt" > "$tmp/pit.txt"
check "the pit is in the world file" "[ \"\$(sed -n 21p $tmp/pit.txt | cut -c164-165)\" = '..' ]"
run pit --world "$tmp/pit.txt" --god --warp 158 --difficulty 0 --frames 4000
check "the boss stays out of the pit" "! grep -q 'boss fell out of the world' $tmp/pit/gloamwood_log.txt"
check "...and the fight goes on (got: $(sed 's/.*boss=\([a-z]*\).*/\1/' $tmp/pit/last.txt))" "grep -q 'boss=fighting' $tmp/pit/last.txt"

rm -rf "$tmp"
[ $fail = 0 ] && echo "boss flows ok"
exit $fail
