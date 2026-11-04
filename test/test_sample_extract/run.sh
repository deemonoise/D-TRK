#!/usr/bin/env bash
# Portable project commit and actual ESP bank-client/codec tests; no PlatformIO, USB or SD.
set -eu
task_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$task_root"
task_build="$(mktemp -d /tmp/dtrk-sample-extract.XXXXXX)"
trap 'rm -rf "$task_build"' EXIT
task_unity=.pio/libdeps/native/Unity/src
task_flags=(-g -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections)
task_link=(-Wl,--gc-sections)
if [ "$(uname -s)" = Darwin ]; then task_link=(-Wl,-dead_strip); fi
"${CC:-clang}" "${task_flags[@]}" -I"$task_unity" -c "$task_unity/unity.c" -o "$task_build/unity.o"
"${CXX:-clang++}" -std=c++17 "${task_flags[@]}" -Ilib/core/src -I"$task_unity" \
  test/test_sample_extract/test_main.cpp lib/core/src/sample_extract.cpp lib/core/src/sample_set.cpp \
  lib/core/src/slices.cpp lib/core/src/model.cpp lib/core/src/bytebeat_lib.cpp lib/core/src/perf_fx.cpp \
  lib/core/src/file_rules.cpp "$task_build/unity.o" "${task_link[@]}" -o "$task_build/core"
"$task_build/core"
"${CXX:-clang++}" -std=c++17 "${task_flags[@]}" -Itest/test_sample_extract/stubs -Isrc -Ilib/core/src -I"$task_unity" \
  -x c++ test/test_sample_extract/client_cases.inc src/link/bank_client.cpp lib/core/src/link_msg.cpp lib/core/src/sample_extract.cpp \
  -x none "$task_build/unity.o" "${task_link[@]}" -o "$task_build/client"
"$task_build/client"
