#!/usr/bin/env bash
# Full-source StoreJob/PCM integration under ASan/UBSan. No archive, PlatformIO or device access.
set -eu
task_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$task_root"
task_build="$(mktemp -d /tmp/dtrk-store-ops.XXXXXX)"
trap 'rm -rf "$task_build"' EXIT
task_unity=.pio/libdeps/native/Unity/src
task_flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections)
task_link=(-Wl,--gc-sections)
if [ "$(uname -s)" = Darwin ]; then task_link=(-Wl,-dead_strip); fi
"${CC:-clang}" "${task_flags[@]}" -I"$task_unity" -c "$task_unity/unity.c" -o "$task_build/unity.o"
"${CXX:-clang++}" -std=c++17 "${task_flags[@]}" -Ilib/core/src -I"$task_unity" \
  test/test_sample_store/test_main.cpp lib/core/src/sample_store.cpp lib/core/src/bank_ops.cpp \
  lib/core/src/sample_bank.cpp lib/core/src/wav.cpp lib/core/src/wave_peaks.cpp lib/core/src/sample_set.cpp \
  lib/core/src/slices.cpp lib/core/src/project_io.cpp lib/core/src/model.cpp lib/core/src/bytebeat_lib.cpp \
  lib/core/src/perf_fx.cpp lib/core/src/file_rules.cpp lib/core/src/time_stretch.cpp \
  lib/core/src/tempo_detect.cpp lib/core/src/onset.cpp "$task_build/unity.o" "${task_link[@]}" -o "$task_build/test"
"$task_build/test"
