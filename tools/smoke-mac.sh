#!/bin/sh
# Check a packaged Mac app the way a player meets it: unzip it, check its signature and its libraries, then start it
# for each kind of Mac this machine can run, from "/" like Finder does, and see that it opens a window, draws the
# opening picture and the game, and saves into its own folder.
#   tools/smoke-mac.sh dist/frans-demo-mac.zip [folder to keep the screenshots in]
# The app saves into the real save folder of whoever runs this (macOS gives an app its home folder whatever $HOME says):
# a screenshot.png there is replaced and the game's usual files are rewritten, nothing else is touched.
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

save="$HOME/Library/Application Support/Fran's demo"
fail() { echo "SMOKE TEST FAILED: $*" >&2; exit 1; }
[ -n "${CI:-}" ] || [ -n "${SMOKE_ALLOW_REAL_HOME:-}" ] || fail "this plays the app into your real save folder ($save); set SMOKE_ALLOW_REAL_HOME=1 if that is fine"

ditto -x -k "$zip" "$tmp/unzipped"
app="$tmp/unzipped/Fran's demo.app"
exe="$app/Contents/MacOS/frans-demo"
[ -x "$exe" ] || fail "no program at Fran's demo.app/Contents/MacOS/frans-demo"
[ -f "$tmp/unzipped/READ ME FIRST.txt" ] || fail "no READ ME FIRST.txt next to the app"
[ -f "$app/Contents/Resources/icon.icns" ] || fail "no icon in the app"
plutil -lint "$app/Contents/Info.plist" || fail "Info.plist is not valid"
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist")" = frans-demo ] || fail "Info.plist names a different program than the one in the app"
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
    log="$tmp/run-$a-$label.log"
    echo "--- $a, $label: $frames frames"
    rm -f "$save/screenshot.png"
    set +e
    (cd / && env GE_QUIT_AFTER="$frames" ${SMOKE_VIDEO:+SDL_VIDEODRIVER=$SMOKE_VIDEO} \
         perl -e 'alarm shift; exec @ARGV' 90 arch "-$a" "$exe" "$@") > "$log" 2>&1
    rc=$?
    set -e
    LC_ALL=C sed 's/^/    | /' "$log"
    [ "$rc" = 0 ] || fail "$a $label: the app ended with status $rc"
    grep -q "^saving in $save/\$" "$log" || fail "$a $label: the app did not move into its save folder '$save/'"
    [ -f "$save/screenshot.png" ] || fail "$a $label: nothing was saved in '$save'"
    [ -f "$save/gloamwood_profile.txt" ] || fail "$a $label: no Paragon profile in the save folder"
    [ -z "$keep" ] || cp "$save/screenshot.png" "$keep/$a-$label.png"
    python3 -I "$here/png_stats.py" "$save/screenshot.png" --min-colors 100 --width 100 || fail "$a $label: the window drew a blank picture"
}

# desktop <arch>: leave the game open for a few seconds and photograph the whole screen, for the screenshots
# (best effort: a Mac with no screen or no permission to photograph it just skips this)
desktop() {
    a="$1"
    [ -n "$keep" ] && [ -z "${SMOKE_VIDEO:-}" ] || return 0
    (cd / && env GE_QUIT_AFTER=900 arch "-$a" "$exe" --no-intro > "$tmp/desktop-$a.log" 2>&1) &
    sleep 5
    screencapture -x "$keep/desktop-$a.png" || echo "(could not photograph the screen)"
    pkill -f "$exe" || true
    wait || true
}

ran=0
for a in $have; do
    if ! arch "-$a" /usr/bin/true 2> /dev/null; then echo "(this Mac can't run $a code: skipped)"; continue; fi
    run "$a" intro 40
    run "$a" game 150 --no-intro
    desktop "$a"
    ran=$((ran + 1))
done
[ "$ran" -gt 0 ] || fail "this Mac could not run any of: $have"
echo "smoke test passed ($ran kind(s) of Mac run)"
