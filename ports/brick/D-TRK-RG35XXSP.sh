#!/bin/sh
set -eu
port_dir=$(CDPATH= cd -- "$(dirname -- "$0")/d-trk-rg35xxsp" && pwd)
export DTRK_DATA="${DTRK_DATA:-/userdata/saves/d-trk-rg35xxsp}"
mkdir -p "$DTRK_DATA"
cd "$port_dir"
exec >"$DTRK_DATA/launcher.log" 2>&1
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-mali}"
export SDL_RENDER_SCALE_QUALITY="${SDL_RENDER_SCALE_QUALITY:-linear}"
export DTRK_SP_NATIVE_TEXT="${DTRK_SP_NATIVE_TEXT:-1}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export AUDIODEV="${AUDIODEV:-default}"
# The SP's codec is playback-only. PipeWire's monitor is not a microphone.
export DTRK_NO_CAPTURE=1
exec ./d-trk "$@"
