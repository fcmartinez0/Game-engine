#!/bin/sh
# The world editor, used the way a person would: scripted stylus taps and drags on the bottom screen, button
# presses, then a look at what it did (the play log, the files it wrote).  editor.sh <demo_headless>
# Bottom-screen points: the map's cell (c, r) is at x = c*8+4, y = 24+r*8+4 while the map's corner is (0, 0)
# (it is, in the Gloamwood at the start). Brushes are at x = k*32+16, y = 228; tabs at y = 204.
bin="$1"
tmp="$(mktemp -d)"
fail=0
check() {   # check <name> <condition...>
    name="$1"; shift
    if "$@"; then :; else echo "FAILED: $name"; fail=1; fi
}
run() {     # run <dir> <args...>: in its own folder (it writes mods/ there), log to run.log
    dir="$1"; shift
    mkdir -p "$dir"
    (cd "$dir" && GLOAM_LOG=1 "$bin" --fresh --profile p.txt --no-hints "$@" > out.txt 2> run.log)
}
has() { grep -q "$2" "$1"; }

# 1. Ten inputs to a working puzzle: START, LEVER, the map, GATE, the map, START (play), walk, slash.
d="$tmp/lever"
run "$d" --no-mods --frames 170 --press "START@30,START@80,RIGHT@92-99,Y@110" --touch "16,228@40;60,116@50;80,228@60;76,116@70"
check "lever placed" has "$d/run.log" "editor: placed lever at 7,11 in gloamwood"
check "gate placed" has "$d/run.log" "editor: placed gate at 9,11 in gloamwood"
check "test play" has "$d/run.log" "editor: test play in gloamwood from 5,11"
check "lever hit" has "$d/run.log" "lever (wire 1, once) on in gloamwood"
check "gate opened" has "$d/run.log" "gate at 9,11 open in gloamwood"
check "autosaved on play" test -f "$d/mods/world.autosave.txt"

# 2. A lever dragged two tiles and saved is there the next time the game starts; saving again keeps a .bak.
d="$tmp/save"
run "$d" --no-mods --frames 120 --touch "16,228@40;60,116@50;60,116>92,116@60-75;224,12@90" --press "START@30"
check "lever moved" has "$d/run.log" "editor: moved lever to 11,11 in gloamwood"
check "saved" has "$d/run.log" "editor: saved 1 rooms to"
check "lever in world.txt" has "$d/mods/world.txt" "^thing lever 11 11 ch=1 mode=once"
check "no autosave left after SAVE" test ! -f "$d/mods/world.autosave.txt"
run "$d" --frames 60 --press "START@30" --touch "224,12@40"
check "loaded next time" has "$d/run.log" "world: mods/world.txt: 1 rooms replaced, 0 added"
check "second save keeps a .bak" test -f "$d/mods/world.bak"

# 3. UNDO takes back a whole stroke: painted, undone and saved, the world is the built-in one again.
d="$tmp/undo"
run "$d" --no-mods --frames 100 --press "START@30" --touch "40,204@40;16,228@45;100,60>180,60@50-60;32,12@70;224,12@80"
check "painted" has "$d/run.log" "editor: painted in gloamwood"
check "undone" has "$d/run.log" "editor: undo in gloamwood"
(cd "$d" && "$bin" --export-world a.txt 2>/dev/null && "$bin" --no-mods --export-world b.txt 2>/dev/null)
check "undo restored every tile" cmp -s "$d/a.txt" "$d/b.txt"

# 4. A world.txt that doesn't load is never written over: SAVE keeps it as world-broken.txt.
d="$tmp/broken"
mkdir -p "$d/mods"
printf 'gloamwood-world 1\nroom bad 3 3\n' > "$d/mods/world.txt"
cp "$d/mods/world.txt" "$d/garbage.txt"
run "$d" --frames 80 --press "START@30" --touch "16,228@40;60,116@50;224,12@60"
check "load failed as it should" has "$d/run.log" "world: mods/world.txt line"
check "kept aside" cmp -s "$d/mods/world-broken.txt" "$d/garbage.txt"
check "new world.txt loads" sh -c "cd '$d' && '$bin' --export-world c.txt 2>&1 | grep -q 'world written'"

# 5. NEW ROOM: a cave with a door that leads back beside the marker, linked both ways, saved.
d="$tmp/room"
run "$d" --no-mods --frames 90 --press "START@30" --touch "160,12@40;200,158@50;290,12@60;224,12@70"
check "room made" has "$d/run.log" "editor: new room room1 (door linked back)"
check "room saved" has "$d/mods/world.txt" "^room room1 40 15 cave"
check "its door leads back" has "$d/mods/world.txt" 'thing door 5 11 id="door1" to="gloamwood:door1"'
check "the way back leads in" has "$d/mods/world.txt" 'thing door 6 11 id="door1" to="room1:door1"'

# 6. Testing costs nothing: STOP EDITING keeps playing, and the editor never ran during the scenes.
d="$tmp/stop"
run "$d" --no-mods --frames 120 --press "START@30,START@40" --touch "240,99@60;240,99@70"
check "stopped" has "$d/run.log" "editor: editing stopped, playing on in gloamwood"
check "playing for real" has "$d/out.txt" "editing=0"

# 7. A sign's words, typed on the on-screen keyboard (I ' M), are saved as typed.
d="$tmp/sign"
run "$d" --no-mods --frames 140 --press "START@30" --touch "200,204@40;80,228@50;140,116@60;240,156@70;304,180@75;208,204@80;276,228@90;224,12@100"
check "sign placed" has "$d/run.log" "editor: placed sign at 17,11 in gloamwood"
check "sign words saved" has "$d/mods/world.txt" "^thing sign 17 11 text=\"I'M\""

# 8. Browsing LEADS TO (taps and a hold) never breaks a working pair of doors: the built-in way down stays.
d="$tmp/browse"
run "$d" --no-mods --frames 240 --press "START@30" --touch "160,12@40;200,158@50;290,12@55;44,116@60;95,204@70;95,204@80;95,204@90;95,204@100-170;224,12@200"
check "the Gloamwood's door to the Hollow is still linked" has "$d/mods/world.txt" 'thing door 2 11 id="hollow" to="hollow:in"'

# 9. Removing a door unlinks the one that led to it, and a new door doesn't inherit the old link.
d="$tmp/door"
run "$d" --no-mods --frames 130 --press "START@30" --touch "160,12@40;200,158@50;290,12@55;96,12@60;44,116@65;200,204@70;16,228@75;164,116@80;224,12@100"
check "the removed door's partner is unlinked" has "$d/mods/world.txt" 'thing door 6 11 id="door1" to=""'
check "the new door is not linked to anything" has "$d/mods/world.txt" 'thing door 20 11 id="door1" to=""'

# 10. The editor's bottom screen is the same after a test play as before it (no stale toolbar, no stray pixels).
sum_of() { grep -o 'bottom=[0-9a-f]*' "$1/out.txt"; }
run "$tmp/plain" --no-mods --frames 200 --press "START@30"
run "$tmp/tested" --no-mods --frames 200 --press "START@30,START@80,START@120,START@150,START@180"
check "bottom screen after test plays" test "$(sum_of "$tmp/plain")" = "$(sum_of "$tmp/tested")"

# 11. An earlier session's autosave isn't overwritten by this session's; RESTORE AUTOSAVE brings it back.
d="$tmp/recover"
run "$d" --no-mods --frames 120 --press "START@30" --touch "16,228@40;60,116@50"
check "autosaved at quit" has "$d/mods/world.autosave.txt" "^thing lever 7 11"
run "$d" --no-mods --frames 120 --press "START@30,START@90" --touch "80,228@40;76,116@50"
check "the earlier autosave is kept aside" has "$d/mods/world.recover.txt" "^thing lever 7 11"
check "this session's autosave is its own" has "$d/mods/world.autosave.txt" "^thing gate 9 11"
rm -f "$d/mods/world.txt"
run "$d" --no-mods --frames 130 --press "START@30" --touch "160,12@40;200,186@50;200,186@60;300,12@70;224,12@80"
check "restored and saved" has "$d/mods/world.txt" "^thing gate 9 11"
check "the autosave was used up" test ! -f "$d/mods/world.recover.txt"

# 12. SAVE keeps an older world.bak when there is no world.txt to take its place.
d="$tmp/bak"
mkdir -p "$d/mods"
echo "an older backup" > "$d/mods/world.bak"
cp "$d/mods/world.bak" "$d/sentinel.txt"
run "$d" --no-mods --frames 100 --press "START@30" --touch "16,228@40;60,116@50;224,12@70"
check "saved" has "$d/mods/world.txt" "^thing lever 7 11"
check "world.bak untouched" cmp -s "$d/mods/world.bak" "$d/sentinel.txt"

# 13. A save cut off between its two renames (no world.txt, a whole world.tmp) is found at the next start.
d="$tmp/cut"
run "$d" --no-mods --frames 100 --press "START@30" --touch "16,228@40;60,116@50;224,12@70"
mv "$d/mods/world.txt" "$d/mods/world.tmp"
run "$d" --frames 20
check "world.tmp is used" has "$d/run.log" "found mods/world.tmp from a save that was cut off"
check "and is world.txt again" test -f "$d/mods/world.txt"

rm -rf "$tmp"
[ $fail = 0 ] && echo "editor: all checks passed"
exit $fail
