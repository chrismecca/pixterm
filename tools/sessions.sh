#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Runs the session test (tests/integration) in its container: a session
# of every kind, from the chooser to logout. The log is kept in
# out/sessions.log and pictures of the screen in out/shots/.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
name=pixterm-sessions
docker build -q -t pixterm-ci "$root/tools/ci" >/dev/null
docker build -q -t "$name" "$root/tests/integration" >/dev/null
docker rm -f "$name" >/dev/null 2>&1 || true
# A network of its own: XDMCP broadcasts must not reach other containers.
docker network inspect "$name" >/dev/null 2>&1 || docker network create "$name" >/dev/null
docker run -d --init --name "$name" --network "$name" -v "$root":/src "$name" \
	sleep infinity >/dev/null
trap 'docker rm -f "$name" >/dev/null' EXIT

mkdir -p "$root/out"
docker exec "$name" /src/tests/integration/setup.sh
sleep 5
status=0
docker exec "$name" /src/tests/integration/scenario.sh >"$root/out/sessions.log" 2>&1 || status=1
grep -E '^(FAIL|==)|failure' "$root/out/sessions.log"
exit $status
