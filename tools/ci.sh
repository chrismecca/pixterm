#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Runs every check that needs no hardware, in the toolchain container.
# GitHub Actions runs this same script.
#
#   tools/ci.sh            all checks
#   tools/ci.sh build      only the named steps: format, ascii, reuse,
#                          build, sanitize, analysis, fuzz, sessions
#   FUZZ_SECONDS=300 tools/ci.sh fuzz    fuzz each parser for longer
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
steps=${*:-format ascii reuse build sanitize analysis fuzz sessions}
fuzz_seconds=${FUZZ_SECONDS:-30}
fail=0

docker build -q -t pixterm-ci tools/ci >/dev/null
# run_tests: the unit tests, showing only the summary when they pass.
in_container() {
	docker run --rm -v "$root":/src -w /src pixterm-ci sh -ec "
		run_tests() {
			if ctest --test-dir /tmp/build --output-on-failure --no-tests=error -j\"\$(nproc)\" \
				>/tmp/ctest.log 2>&1; then
				tail -n 3 /tmp/ctest.log
			else
				cat /tmp/ctest.log
				return 1
			fi
		}
		$1"
}

run_step() {
	printf '\n=== %s\n' "$1"
	if ! in_container "$2"; then
		printf '=== %s FAILED\n' "$1"
		fail=1
	fi
}

for step in $steps; do
	case $step in
	format)
		run_step "format" \
			'git ls-files "*.cpp" "*.hpp" | xargs clang-format --dry-run --Werror'
		;;
	ascii)
		run_step "plain ASCII" \
			'! git ls-files -z | xargs -0 grep -IlP "[^\\x00-\\x7F]"'
		;;
	reuse)
		run_step "licence headers (REUSE)" 'reuse lint --lines'
		;;
	build)
		run_step "build with warnings as errors, and unit tests" \
			'cmake --preset ci -B /tmp/build >/dev/null &&
			 cmake --build /tmp/build &&
			 run_tests'
		;;
	sanitize)
		run_step "unit tests with ASan and UBSan" \
			'cmake --preset sanitize -B /tmp/build >/dev/null &&
			 cmake --build /tmp/build &&
			 run_tests'
		;;
	analysis)
		run_step "clang-tidy" \
			'cmake --preset analysis -B /tmp/build >/dev/null && cmake --build /tmp/build -- -k 0'
		;;
	fuzz)
		run_step "fuzzing, ${fuzz_seconds}s for each parser" "tools/fuzz.sh $fuzz_seconds"
		;;
	sessions)
		printf '\n=== sessions of every kind, on Xvfb\n'
		tools/sessions.sh || { echo '=== sessions FAILED'; fail=1; }
		;;
	*)
		echo "unknown step: $step" >&2
		exit 2
		;;
	esac
done

printf '\n=== result: %s\n' "$([ $fail = 0 ] && echo 'all checks passed' || echo FAILED)"
exit $fail
