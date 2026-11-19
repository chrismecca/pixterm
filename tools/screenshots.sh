#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Takes the README's pictures (doc/img/*.png): the chooser and Configure,
# with example hosts, on a virtual screen in the session test's
# container. Run it again when the look changes.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
name=pixterm-screenshots
docker build -q -t pixterm-ci "$root/tools/ci" >/dev/null
docker build -q -t pixterm-sessions "$root/tests/integration" >/dev/null
docker rm -f "$name" >/dev/null 2>&1 || true
docker run -d --init --name "$name" -v "$root":/src pixterm-sessions sleep infinity >/dev/null
trap 'docker rm -f pixterm-screenshots >/dev/null' EXIT

docker exec -w /src "$name" sh -ec '
cmake --preset release -B /tmp/b -DCMAKE_INSTALL_PREFIX=/usr -DPIXTERM_SYSTEM_FILES=OFF >/dev/null
cmake --build /tmp/b >/dev/null
cmake --install /tmp/b >/dev/null
mkdir -p /tmp/state
cat > /tmp/config.json <<JSON
{
  "schema_version": 1,
  "hosts": [
    { "id": "a", "name": "Workstation", "address": "ws.example.org", "protocol": "ssh-x11",
      "username": "alice" },
    { "id": "b", "name": "Origin 200", "address": "192.0.2.16", "protocol": "xdmcp" },
    { "id": "c", "name": "OpenVMS", "address": "vms.example.org", "protocol": "ssh-x11",
      "username": "ALICE", "session": "raw", "command": "RUN SYS\$SYSTEM:DECW\$SESSION" },
    { "id": "d", "name": "Windows desktop", "address": "win.example.org", "protocol": "rdp",
      "username": "alice" },
    { "id": "e", "name": "Mainframe", "address": "mvs.example.org", "protocol": "tn3270" },
    { "id": "f", "name": "Plan 9", "address": "cpu.example.org", "protocol": "drawterm",
      "username": "glenda" },
    { "id": "g", "name": "Sun console", "protocol": "serial", "device": "/dev/ttyUSB0" }
  ]
}
JSON
Xvfb :7 -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
sleep 2
export DISPLAY=:7 XENVIRONMENT=/usr/share/pixterm/Xresources PIXTERM_CONFIG=/tmp/config.json \
    PIXTERM_STATE_DIR=/tmp/state
mwm >/dev/null 2>&1 &
sleep 1
/usr/libexec/pixterm/pixterm-ui >/dev/null 2>&1 &
sleep 3
# The "not started by pixtermd" notice: close it.
xdotool search --sync --classname errorDialog_popup >/dev/null && xdotool key Return
sleep 1
mkdir -p /src/doc/img
# The window and its frame, with a margin of the root pattern.
shot() {
	w=$(xdotool search --name "$1" | tail -1)
	eval "$(xdotool getwindowgeometry --shell "$w")"
	import -window root -crop "$((WIDTH + 64))x$((HEIGHT + 88))+$((X - 32))+$((Y - 56))" +repage \
	    -colors 16 "/src/doc/img/$2.png"
}
shot "pixterm - Network Terminal" chooser
w=$(xdotool search --name "pixterm - Network Terminal" | tail -1)
eval "$(xdotool getwindowgeometry --shell "$w")"
xdotool mousemove $((X + WIDTH * 82 / 100)) $((Y + HEIGHT - 83)) click 1
xdotool search --sync --name "pixterm Configuration" >/dev/null
sleep 1
shot "pixterm Configuration" configure
'
ls -l "$root"/doc/img/*.png
