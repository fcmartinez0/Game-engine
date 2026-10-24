#!/bin/sh
# Check a packaged Mac app the way a player meets it: unzip it, check its signature and its libraries, then start it
# for each kind of Mac this machine can run, from "/" like Finder does, and see that it opens a window, draws the
# opening picture and the game, and saves into its own folder.
#   tools/smoke-mac.sh dist/frans-demo-mac.zip [folder to keep the screenshots in]
# SMOKE_VIDEO=dummy runs it without a screen (SDL's dummy video driver), for a Mac with no desktop session.
# SMOKE_ARCHS="arm64" says which kinds of Mac the app must contain (default: "arm64 x86_64").
set -eu

zip="${1:?usage: smoke-mac.sh <frans-demo-mac.zip> [folder for screenshots]}"
keep="${2:-}"
want="${SMOKE_ARCHS:-arm64 x86_64}"
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/frans-smoke.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
[ -z "$keep" ] || mkdir -p "$keep"

real_home="$HOME"
fail() { echo "SMOKE TEST FAILED: $*" >&2; exit 1; }

ditto -x -k "$zip" "$tmp/unzipped"
app="$tmp/unzipped/Fran's demo.app"
exe="$app/Contents/MacOS/frans-demo"
[ -x "$exe" ] || fail "no program at Fran's demo.app/Contents/MacOS/frans-demo"
[ -f "$tmp/unzipped/READ ME FIRST.txt" ] || fail "no READ ME FIRST.txt next to the app"
[ -f "$app/Contents/Resources/icon.icns" ] || fail "no icon in the app"
plutil -lint "$app/Contents/Info.plist" || fail "Info.plist is not valid"
codesign --verify --deep --strict --verbose=2 "$app" || fail "the signature does not verify"

have="$(lipo -archs "$exe")"
echo "the app contains: $have"
for a in $want; do
    case " $have " in *" $a "*) ;; *) fail "the app has no $a code (it has: $have)";; esac
done
stray="$(otool -L "$exe" | grep '^[[:space:]]' | awk '{print $1}' | grep -v -E '^(/usr/lib/|/System/Library/)' || true)"
[ -z "$stray" ] || fail "the program needs libraries other Macs don't have: $stray"

# run <arch> <name> <frames> [game options]: start the app from "/", let it show <frames> frames, keep its screenshot
run() {
    a="$1"; label="$2"; frames="$3"; shift 3
    home="$tmp/home-$a-$label"
    mkdir -p "$home"
    save="$home/Library/Application Support/Fran's demo"
    log="$tmp/run-$a-$label.log"
    echo "--- $a, $label: $frames frames"
    touch "$tmp/marker"
    sleep 1
    set +e
    (cd / && env HOME="$home" GE_QUIT_AFTER="$frames" ${SMOKE_VIDEO:+SDL_VIDEODRIVER=$SMOKE_VIDEO} \
         perl -e 'alarm shift; exec @ARGV' 90 arch "-$a" "$exe" "$@") > "$log" 2>&1
    rc=$?
    set -e
    sed 's/^/    | /' "$log"
    [ "$rc" = 0 ] || fail "$a $label: the app ended with status $rc"
    real_save="$real_home/Library/Application Support/Fran's demo"
    if [ ! -f "$save/screenshot.png" ] && [ "$real_save/screenshot.png" -nt "$tmp/marker" ]; then
        echo "    (macOS ignored HOME: the app saved in the real home folder, '$real_save')"
        save="$real_save"
    fi
    [ -f "$save/screenshot.png" ] || fail "$a $label: nothing was saved in '$save' (does the app save where it should?)"
    [ -f "$save/gloamwood_profile.txt" ] || fail "$a $label: no Paragon profile in the save folder"
    python3 -I "$here/png_stats.py" "$save/screenshot.png" --min-colors 100 --width 100 || fail "$a $label: the window drew a blank picture"
    [ -z "$keep" ] || cp "$save/screenshot.png" "$keep/$a-$label.png"
}

ran=0
for a in $have; do
    if ! arch "-$a" /usr/bin/true 2> /dev/null; then echo "(this Mac can't run $a code: skipped)"; continue; fi
    run "$a" intro 40
    run "$a" game 150 --no-intro
    ran=$((ran + 1))
done
[ "$ran" -gt 0 ] || fail "this Mac could not run any of: $have"
echo "smoke test passed ($ran kind(s) of Mac run)"
