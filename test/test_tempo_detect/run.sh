#!/usr/bin/env bash
# Portable onset and tempo tests only: no PlatformIO, USB, card, or framework changes.
set -eu
task_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$task_root"
task_build="$(mktemp -d /tmp/dtrk-onset-tempo.XXXXXX)"
trap 'rm -rf "$task_build"' EXIT
task_unity=.pio/libdeps/native/Unity/src
task_flags=(-O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
"${CC:-clang}" "${task_flags[@]}" -I"$task_unity" -c "$task_unity/unity.c" -o "$task_build/unity.o"
"${CXX:-clang++}" -std=c++17 "${task_flags[@]}" -Ilib/core/src -I"$task_unity" \
  test/test_onset/test_main.cpp lib/core/src/onset.cpp lib/core/src/slices.cpp "$task_build/unity.o" -o "$task_build/onset"
"$task_build/onset"
"${CXX:-clang++}" -std=c++17 "${task_flags[@]}" -Ilib/core/src -I"$task_unity" \
  test/test_tempo_detect/test_main.cpp lib/core/src/tempo_detect.cpp "$task_build/unity.o" -o "$task_build/tempo"
"$task_build/tempo"
