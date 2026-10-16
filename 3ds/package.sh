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
(cd "$root/dist/sd" && python3 -m zipfile -c ../gloamwood-3ds.zip 3ds)
echo "dist/gloamwood-3ds.zip ready"
