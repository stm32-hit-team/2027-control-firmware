#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/.pio/host-tests"

mkdir -p "$build_dir"
clang \
  -std=c11 \
  -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  -I"$project_dir/lib/rfid_core/include" \
  "$project_dir/test/native/test_main.c" \
  "$project_dir/lib/rfid_core/src/rfid_protocol.c" \
  "$project_dir/lib/rfid_core/src/rfid_reader.c" \
  "$project_dir/lib/rfid_core/src/tts_service.c" \
  -o "$build_dir/native_tests"

"$build_dir/native_tests"
