#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Formats every C++ file with the toolchain container's clang-format,
# the version the format check uses.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
docker build -q -t pixterm-ci "$root/tools/ci" >/dev/null
docker run --rm -v "$root":/src -w /src pixterm-ci \
	sh -c 'git ls-files -co --exclude-standard "*.cpp" "*.hpp" | xargs clang-format -i'
