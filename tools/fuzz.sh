#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Builds the fuzzers and runs each for a while. Runs inside the
# toolchain container (tools/ci.sh fuzz). Inputs that make a fuzzer
# fail are saved in out/fuzz/.
#
#   tools/fuzz.sh [SECONDS]
set -eu

seconds=${1:-30}
build=/tmp/build-fuzz
cmake --preset fuzz -B "$build" -DBUILD_TESTING=OFF >/dev/null
cmake --build "$build" >/dev/null
mkdir -p out/fuzz

for fuzzer in "$build"/tests/fuzz/fuzz_*; do
	name=${fuzzer##*/fuzz_}
	mkdir -p "/tmp/corpus/$name"
	if ! "$fuzzer" -max_total_time="$seconds" -max_len=16384 -artifact_prefix=out/fuzz/ \
		"/tmp/corpus/$name" "tests/fuzz/corpus/$name" >"/tmp/fuzz-$name.log" 2>&1; then
		grep -E 'ERROR|SUMMARY|Test unit written' "/tmp/fuzz-$name.log"
		exit 1
	fi
	printf '%s: %s\n' "$name" "$(grep -E '^Done|DONE' "/tmp/fuzz-$name.log" | tail -n 1)"
done
