#!/usr/bin/env bash
# No PlatformIO, USB, card access or global SDK changes. Run both portable REC and host server tests.
set -eu
task_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$task_root"
task_build="$(mktemp -d /tmp/dtrk-rec-host.XXXXXX)"
trap 'rm -rf "$task_build"' EXIT
task_unity=.pio/libdeps/native/Unity/src
if [ ! -f "$task_unity/unity.c" ]; then
  echo "Missing native Unity dependency: $task_unity/unity.c" >&2
  exit 1
fi
task_cxx="${CXX:-clang++}"
task_cc="${CC:-clang}"
task_flags=(-g -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections)
task_link=(-Wl,--gc-sections)
if [ "$(uname -s)" = Darwin ]; then task_link=(-Wl,-dead_strip); fi
"$task_cc" "${task_flags[@]}" -I"$task_unity" -c "$task_unity/unity.c" -o "$task_build/unity.o"
task_sources=(lib/core/src/rec.cpp lib/core/src/wav.cpp lib/core/src/sample_set.cpp
  lib/core/src/slices.cpp lib/core/src/project_io.cpp)
"$task_cxx" -std=c++17 "${task_flags[@]}" -Ilib/core/src -I"$task_unity" \
  test/test_rec/test_main.cpp "${task_sources[@]}" lib/core/src/model.cpp \
  lib/core/src/file_rules.cpp lib/core/src/bytebeat_lib.cpp lib/core/src/perf_fx.cpp \
  "$task_build/unity.o" "${task_link[@]}" -o "$task_build/core"
"$task_build/core"
"$task_cxx" -std=c++17 "${task_flags[@]}" -Itest/test_rec_server/stubs -Isrc/teensy \
  -Ilib/core/src -I"$task_unity" -x c++ test/test_rec_server/test_cases.inc \
  "${task_sources[@]}" lib/core/src/link_msg.cpp -x none "$task_build/unity.o" \
  "${task_link[@]}" -o "$task_build/server"
"$task_build/server"
