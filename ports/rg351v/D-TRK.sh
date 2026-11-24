#!/bin/sh
set -eu
port_dir=$(CDPATH= cd -- "$(dirname -- "$0")/d-trk-rg351v" && pwd -P)
roms_dir=$(CDPATH= cd -- "$port_dir/../.." && pwd -P)
export DTRK_DATA="${DTRK_DATA:-$roms_dir/saves/d-trk-rg351v}"
mkdir -p "$DTRK_DATA"
cd "$port_dir"
exec >"$DTRK_DATA/launcher.log" 2>&1
export DTRK_DEVICE=rg351v
export DTRK_SP_NATIVE_TEXT="${DTRK_SP_NATIVE_TEXT:-1}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
# SDL uses positional face buttons. Map them to the RG351V's printed labels in D-TRK.
# Both ArkOS GUID variants use b6/b7 for Start/Select and b10/b11 for L2/R2.
if [ -z "${SDL_GAMECONTROLLERCONFIG:-}${SDL_GAMECONTROLLERCONFIG_FILE:-}" ]; then
    export SDL_GAMECONTROLLERCONFIG='03000000091200000031000011010000,RG351V,a:b1,b:b0,x:b3,y:b2,back:b7,start:b6,leftshoulder:b4,rightshoulder:b5,lefttrigger:b10,righttrigger:b11,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,platform:Linux,
03002758091200000031000011010000,RG351V,a:b1,b:b0,x:b3,y:b2,back:b7,start:b6,leftshoulder:b4,rightshoulder:b5,lefttrigger:b10,righttrigger:b11,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,crc:5827,platform:Linux,'
fi
exec ./d-trk "$@"
