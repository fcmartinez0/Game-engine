#!/bin/sh
# Build gloamwood.3dsx and pack dist/gloamwood-3ds.zip, laid out like the SD card:
#   3ds/gloamwood/gloamwood.3dsx + README.txt   (the play log is written next to them)
# Uses a local devkitPro if $DEVKITPRO is set, otherwise devkitPro's Docker image.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
if [ -n "$DEVKITPRO" ]; then
    make -C "$here"
else
    docker run --rm -v "$root":/src -w /src/3ds devkitpro/devkitarm make
fi
rm -rf "$root/dist/sd" "$root/dist/gloamwood-3ds.zip"
mkdir -p "$root/dist/sd/3ds/gloamwood"
cp "$here/gloamwood.3dsx" "$root/dist/sd/3ds/gloamwood/"
cp "$here/README-3DS.txt" "$root/dist/sd/3ds/gloamwood/README.txt"
mkdir -p "$root/dist/sd/3ds/gloamwood/mods"
cat > "$root/dist/sd/3ds/gloamwood/mods/README.txt" <<'TXT'
Put changed character files here (hero.anim, hero.pam, hero.moves, ...).
To get the built-in ones to start from: dev menu (bottom screen) > EXPORT CHARACTERS.
They appear in mods/_export/ with a README that explains each file.
Then: dev menu > RELOAD MODS.
TXT
(cd "$root/dist/sd" && python3 -m zipfile -c ../gloamwood-3ds.zip 3ds)
echo "dist/gloamwood-3ds.zip ready"
