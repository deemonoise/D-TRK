#!/usr/bin/env bash
set -eu
task_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$task_root"
task_build="$(mktemp -d /tmp/dtrk-edit-host.XXXXXX)"
trap 'rm -rf "$task_build"' EXIT
python3 - "$task_build/session.inc" <<'PY'
from pathlib import Path
import sys
source=Path('src/teensy/bank.cpp').read_text()
start='// ---- Offline SampleEdit session ----'
end='// ---- End SampleEdit session ----'
if start in source and end in source:
    code=source.split(start,1)[1].split(end,1)[0]
else:
    # Baseline has no SampleEdit handler. Keep that unsupported behavior executable for RED,
    # rather than failing compilation because the session API does not exist yet.
    code='''struct SampleEditSession {
      bool active=false,cached=false;
      int handle(uint8_t,const uint8_t*,int,uint8_t*) { return 0; }
      int cancel(const uint8_t*,int) { return -1; }
      void reset() {}
      bool step() { return false; }
    } edit;'''
Path(sys.argv[1]).write_text(code)
PY
task_unity=.pio/libdeps/native/Unity/src
task_flags=(-g -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections)
task_link=(-Wl,--gc-sections)
if [ "$(uname -s)" = Darwin ]; then task_link=(-Wl,-dead_strip); fi
clang "${task_flags[@]}" -I"$task_unity" -c "$task_unity/unity.c" -o "$task_build/unity.o"
clang++ -std=c++17 "${task_flags[@]}" -Ilib/core/src -Isrc/teensy -I"$task_unity" \
  -DBANK_EDIT_EXTRACT="\"$task_build/session.inc\"" -x c++ test/test_sample_edit_server/test_cases.inc \
  lib/core/src/link_msg.cpp -x none "$task_build/unity.o" "${task_link[@]}" -o "$task_build/test"
"$task_build/test"
