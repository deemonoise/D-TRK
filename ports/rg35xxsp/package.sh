#!/bin/sh
set -eu
port_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export DTRK_TARGET=rg35xxsp
exec "$port_dir/../common/package.sh" "$@"
