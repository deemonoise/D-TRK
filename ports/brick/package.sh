#!/bin/sh
set -eu
port_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$port_dir/../common/package.sh" "$@"
