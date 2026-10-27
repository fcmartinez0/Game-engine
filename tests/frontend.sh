#!/bin/sh
# The screens around the game and the saved game, used the way a person would: the title after the picture, NEW GAME
# and its story, resting at a bench (which saves), CONTINUE, the pause menu, quitting, the credits.
#   frontend.sh <demo_headless>
bin="$1"
tmp="$(mktemp -d)"
fail=0
check() {   # check <name> <condition...>
    name="$1"; shift
    if "$@"; then :; else echo "FAILED: $name"; fail=1; fi
}
run() {     # run <dir> <args...>: in its own folder (the save and the settings go there), log to run.log, status to out.txt
    dir="$1"; shift
    mkdir -p "$dir"
    (cd "$dir" && GLOAM_LOG=1 "$bin" --no-mods --fresh --profile p.txt --no-hints "$@" > out.txt 2> run.log)
}
has() { grep -q "$2" "$1"; }
field() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" "$1/out.txt"; }   # field <dir> <name>: a value from the status line

# a normal start: the picture, then the title (the game waits behind both)
d="$tmp/start"
run "$d" --frames 200
check "the title follows the picture" [ "$(field "$d" front)" = 1 ]
check "the title says so in the log" has "$d/run.log" "title screen"
run "$d" --warp 20 --frames 30
check "a scripted run plays at once" [ "$(field "$d" front)" = 0 ]
run "$d" --no-title --frames 30
check "--no-title plays at once" [ "$(field "$d" front)" = 0 ]

# NEW GAME (the first choice when there is no save), its story, and the story skipped
d="$tmp/new"
run "$d" --no-intro --title --save save.txt --frames 120 --press "B@40"
check "NEW GAME tells the story" [ "$(field "$d" front)" = 3 ]
check "new game logged" has "$d/run.log" "new game"
run "$d" --no-intro --title --save save.txt --frames 200 --press "B@40,A@100"
check "the story can be skipped" [ "$(field "$d" front)" = 0 ]
check "leaving a game saves it" has "$d/run.log" "saved (leaving the game)"
check "at the last bench: a new game's is the first one" has "$d/save.txt" "^bench gloamwood 4 11"
d="$tmp/tap"
run "$d" --no-intro --title --save save.txt --frames 120 --touch "160,60@40"
check "a tap on the bottom screen's NEW GAME" [ "$(field "$d" front)" = 3 ]

# resting at a bench saves
d="$tmp/rest"
run "$d" --no-intro --title --save save.txt --frames 260 --press "B@40,A@100,UP@200"
check "resting at a bench saves" has "$d/run.log" "saved (rested at a bench)"
check "the save names the bench" has "$d/save.txt" "^bench gloamwood 4 11"
check "it is whole" has "$d/save.txt" "^end"
check "the saying" has "$d/run.log" "rested at the bench"

# CONTINUE: a save made by hand, in the caves, with a key, a relic taken and a mask from it
d="$tmp/continue"
mkdir -p "$d"
cat > "$d/save.txt" <<'EOF'
fransdemo-save 1
bench hollow 46 11
relics 1 0
keys 1
stats 5 2 1 0 3 7200
ending 0
end
EOF
run "$d" --no-intro --title --save save.txt --frames 120 --press "A@40"
check "CONTINUE is the first choice with a save" has "$d/run.log" "title screen (there is a save)"
check "continued" has "$d/run.log" "continued from the save: hollow, bench at 46,11"
check "back at that bench" [ "$(field "$d" room)" = hollow ] && [ "$(field "$d" front)" = 0 ]
check "with the mask the relic gave" [ "$(field "$d" masks)" = 9 ]   # (easy: 8 masks, and one more)
check "and its numbers" [ "$(field "$d" kills)" = 5 ] && [ "$(field "$d" deaths)" = 2 ]
check "leaving saves the time played" has "$d/save.txt" "^stats 5 2 1 0 3 7[2-9][0-9][0-9]"

# a save from a different world: a room that has changed starts fresh, the rest is kept
d="$tmp/changed"
mkdir -p "$d"
cat > "$d/save.txt" <<'EOF'
fransdemo-save 1
bench gloamwood 105 11
room gloamwood 2 0,1
beaten 0
endroom
end
EOF
run "$d" --no-intro --title --save save.txt --frames 100 --press "A@40"
check "a changed room is noticed" has "$d/run.log" "room gloamwood has changed since it was saved"
check "but the bench is still found" has "$d/run.log" "bench at 105,11"

# a broken save: no CONTINUE (NEW GAME is first), and the game starts fine
d="$tmp/broken"
mkdir -p "$d"
printf 'fransdemo-save 1\nbench gloamwood 105\n' > "$d/save.txt"
run "$d" --no-intro --title --save save.txt --frames 100 --press "B@40"
check "a broken save offers no CONTINUE" has "$d/run.log" "title screen$"
check "NEW GAME then" has "$d/run.log" "new game"

# NEW GAME over a save asks twice, then the save is gone
d="$tmp/over"
mkdir -p "$d"
printf 'fransdemo-save 1\nbench gloamwood 105 11\nend\n' > "$d/save.txt"
run "$d" --no-intro --title --save save.txt --frames 100 --press "DOWN@40,A@42"
check "one press doesn't erase it" test -f "$d/save.txt"
check "and nothing started" [ "$(field "$d" front)" = 1 ]
run "$d" --no-intro --title --save save.txt --frames 100 --press "DOWN@40,A@42,A@44"
check "the second press starts a new game" has "$d/run.log" "new game"
check "and the old save is gone" sh -c "! grep -q 'bench gloamwood 105' '$d/save.txt' 2>/dev/null"

# the pause menu: START opens it, START closes it; the game doesn't move underneath
d="$tmp/pause"
run "$d" --warp 20 --frames 60 --press "START@20"
check "START pauses" [ "$(field "$d" front)" = 2 ]
x_paused=$(field "$d" x)
run "$d" --warp 20 --frames 60 --press "START@20,RIGHT@25-55"
check "the hero doesn't walk while paused" [ "$(field "$d" x)" = "$x_paused" ]
run "$d" --warp 20 --frames 60 --press "START@20,START@40"
check "START again resumes" [ "$(field "$d" front)" = 0 ]
run "$d" --warp 20 --frames 60 --press "START@20,B@40"
check "B resumes too" [ "$(field "$d" front)" = 0 ]

# SAVE AND QUIT TO TITLE: saved, and the title offers CONTINUE
d="$tmp/quit"
run "$d" --no-intro --title --save save.txt --frames 200 --press "B@40,A@100,START@120,DOWN@130,DOWN@132,DOWN@134,A@136"
check "quit to the title saves" has "$d/run.log" "saved (quit to the title)"
check "and shows the title with CONTINUE" has "$d/run.log" "title screen (there is a save)"
check "the title is up" [ "$(field "$d" front)" = 1 ]

# QUIT on the title ends the game early
d="$tmp/leave"
run "$d" --no-intro --title --frames 400 --dump-every 1 --dump-dir . --press "DOWN@40,DOWN@42,A@44"
check "QUIT ends the game" has "$d/run.log" "quit from the title"
check "at once" test ! -f "$d/frame_00100.png"

# the difficulty, from the title: LEFT / RIGHT, and it is kept
d="$tmp/difficulty"
run "$d" --no-intro --title --frames 100 --press "DOWN@40,RIGHT@42"
check "RIGHT on DIFFICULTY makes it normal" has "$d/run.log" "difficulty now normal"
check "and it is kept" test -f "$d/gloamwood_settings.txt"

# the credits: they roll, START jumps to the end, A goes to the title, and the end is saved
d="$tmp/credits"
run "$d" --credits --title --save save.txt --frames 200 --press "START@60,A@120"
check "the end is saved" has "$d/save.txt" "^ending 1"
check "after the credits, the title" [ "$(field "$d" front)" = 1 ]
run "$d" --credits --frames 300
check "the credits wait for a press" [ "$(field "$d" front)" = 4 ]

rm -rf "$tmp"
[ $fail = 0 ] && echo "front end: all checks passed"
exit $fail
