#!/bin/sh
set -eu
port_dir=$(CDPATH= cd -- "$(dirname -- "$0")/d-trk" && pwd)
export DTRK_DATA="${DTRK_DATA:-/userdata/saves/d-trk}"
mkdir -p "$DTRK_DATA"
cd "$port_dir"
exec >"$DTRK_DATA/launcher.log" 2>&1
for arg in "$@"; do
    if [ "$arg" = --self-test ]; then exec ./d-trk "$@"; fi
done
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
exec python3 ./audio_lease.py ./d-trk "$@"
