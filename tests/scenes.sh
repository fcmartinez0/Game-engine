#!/bin/sh
# The benchmark scenes' last frames, against tests/scenes.txt: pixel-exact refactors must not change them.
#   scenes.sh <demo_headless> [--from-file]
# --from-file plays each scene from the built-in world exported to a world file and read back (--world),
# which proves the file a designer edits reproduces the game exactly.
bin="$1"; here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
extra=""
if [ "$2" = "--from-file" ]; then
    "$bin" --no-mods --export-world "$tmp/world.txt" 2>/dev/null || { echo "export failed"; exit 1; }
    extra="--world $tmp/world.txt"
fi
fail=0
while read -r name want args; do
    case "$name" in ''|'#'*) continue ;; esac
    got=$(cd "$tmp" && "$bin" --no-mods --no-hints --fresh --profile "$tmp/p.txt" $extra $args 2>/dev/null | sed -n 's/.*checksum=\([0-9a-f]*\).*/\1/p')
    if [ "$got" != "$want" ]; then echo "$name: got $got, want $want"; fail=1; fi
done < "$here/scenes.txt"
rm -rf "$tmp"
[ $fail = 0 ] && echo "all scenes match"
exit $fail
