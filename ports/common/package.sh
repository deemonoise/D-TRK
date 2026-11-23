#!/bin/sh
# Build the ARM64 port or package an existing cross-built executable.
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [Linux ARM64 executable]" >&2
    exit 2
fi
target=${DTRK_TARGET:-brick}
case "$target" in
    brick) launcher=D-TRK.sh; directory=d-trk ;;
    rg35xxsp) launcher=D-TRK-RG35XXSP.sh; directory=d-trk-rg35xxsp ;;
    *) echo "Unknown DTRK_TARGET: $target" >&2; exit 2 ;;
esac
device="$project/ports/$target"
archive="d-trk-$target.zip"
output="$project/target/brick"
mkdir -p "$output"
stage=$(mktemp -d "$output/.package.XXXXXX")
trap 'rm -rf "$stage"' EXIT
if [ "$#" -eq 0 ]; then
    if [ -z "${BRICK_IMAGE:-}" ]; then
        BRICK_IMAGE=d-trk-knulli:24.04
        docker build --platform linux/arm64 -t "$BRICK_IMAGE" - <"$project/ports/common/Dockerfile"
    fi
    # Copy sources only. This also works when Docker cannot mount the user's project directory.
    COPYFILE_DISABLE=1 tar -cf "$stage/sources.tar" -C "$project" lib/core/src src ports/common ports/brick ports/rg35xxsp
    binary="$stage/d-trk.aarch64"
    docker run --rm -i --platform linux/arm64 --workdir /project "$BRICK_IMAGE" \
        sh -ec 'tar xf -; make -C ports/common -j"$(nproc)" BUILD=../../target/ports/aarch64 >&2
                strip target/ports/aarch64/d-trk
                cat target/ports/aarch64/d-trk' <"$stage/sources.tar" >"$binary"
else
    binary=$1
fi
case "$(LC_ALL=C file -b "$binary")" in
    "ELF 64-bit LSB "*executable*", ARM aarch64"*) ;;
    *) echo "Expected a Linux ARM64 executable: $binary" >&2; exit 1 ;;
esac
mkdir -p "$stage/$directory"
install -m 755 "$binary" "$stage/$directory/d-trk"
install -m 755 "$device/$launcher" "$stage/$launcher"
cp "$project/ports/common/ble_midi_bridge.py" "$stage/$directory/"
if [ "$target" = brick ]; then
    cp "$device/audio_lease.py" "$device/volume_keys.py" "$stage/$directory/"
fi
for readme in README.md README_ru.md; do
    {
        cat "$device/$readme"
        printf '\n'
        # The device guide already has a title and a link to the other language.
        sed -n '/^## /,$p' "$project/ports/common/$readme"
    } >"$stage/$directory/$readme"
done
cp "$project/LICENSE" "$stage/$directory/"
cp -R "$project/LICENSES" "$stage/$directory/"
(cd "$stage" && zip -qr "$archive" "$launcher" "$directory")
mv "$stage/$archive" "$output/"
printf '%s\n' "$output/$archive"
