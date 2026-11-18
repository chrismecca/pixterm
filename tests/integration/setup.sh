#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Builds and installs pixterm with Xvfb in place of Xorg, starts the
# stand-in hosts, then pixterm-helper and pixtermd. Run inside the
# session test container; run it again to restart from scratch.
set -eu
cd /src
pkill -f /usr/libexec/pixterm/pixtermd || true
pkill -f "pixterm-helper --listen" || true
sleep 1
pkill -u pixterm || true
rm -rf /tmp/rt
cmake --preset debug -B /tmp/b -DCMAKE_INSTALL_PREFIX=/usr -DPIXTERM_SYSTEM_FILES=OFF \
    -DPIXTERM_XORG=/src/tests/integration/fake-xorg >/dev/null
cmake --build /tmp/b >/dev/null
cmake --install /tmp/b >/dev/null
setcap cap_net_bind_service=ep /usr/libexec/pixterm/pixterm-rexec
# The programs the image builds from upstream source, built the same way.
if [ ! -x /usr/local/bin/tn5250 ] || [ ! -x /usr/local/bin/drawterm ]; then
	/src/image/linux/third-party.sh fetch /src/out/sources
	for program in tn5250 drawterm; do
		/src/image/linux/third-party.sh build /src/out/sources "$program" /usr/local
	done
fi
cp /tmp/b/tests/tools/helper-call /tmp/helper-call
rm -f /home/alice/.ssh/authorized_keys
cat > /tmp/config.json <<'JSON'
{
  "schema_version": 1,
  "hosts": [
    { "id": "h1", "name": "localhost-xterm", "address": "127.0.0.1",
      "protocol": "ssh-x11", "port": 22, "username": "alice", "session": "xterm" },
    { "id": "h2", "name": "ask-for-user", "address": "127.0.0.1",
      "protocol": "ssh-x11", "username": "", "session": "xterm" },
    { "id": "h3", "name": "unreachable", "address": "192.0.2.1",
      "protocol": "ssh-x11", "username": "alice", "session": "xterm" },
    { "id": "h4", "name": "silent-xdmcp", "address": "192.0.2.1", "protocol": "xdmcp" },
    { "id": "h5", "name": "alice-by-key", "address": "127.0.0.1",
      "protocol": "ssh-x11", "username": "alice", "session": "xterm", "auth": "key" },
    { "id": "h6", "name": "alice-terminal", "address": "127.0.0.1",
      "protocol": "ssh-terminal", "username": "alice" },
    { "id": "h7", "name": "fake-telnet", "address": "127.0.0.1",
      "protocol": "telnet", "port": 2323 },
    { "id": "h8", "name": "closed-telnet", "address": "127.0.0.1",
      "protocol": "telnet", "port": 2324 },
    { "id": "h9", "name": "direct-x-host", "address": "127.0.0.1", "protocol": "direct-x" },
    { "id": "h10", "name": "rexec-host", "address": "127.0.0.1", "protocol": "rexec",
      "port": 5120, "username": "alice", "command": "xterm -display %D" },
    { "id": "h11", "name": "rsh-host", "address": "127.0.0.1", "protocol": "rsh",
      "port": 5140, "username": "alice", "command": "xterm -display %D" },
    { "id": "h12", "name": "vnc-host", "address": "127.0.0.1", "protocol": "vnc", "port": 5905 },
    { "id": "h13", "name": "rdp-host", "address": "127.0.0.1", "protocol": "rdp", "port": 3390,
      "username": "alice" },
    { "id": "h16", "name": "mainframe", "address": "127.0.0.1", "protocol": "tn3270",
      "port": 2325 },
    { "id": "h17", "name": "as400", "address": "127.0.0.1", "protocol": "tn5250",
      "port": 2326 },
    { "id": "h18", "name": "plan9", "address": "127.0.0.1", "protocol": "drawterm",
      "port": 17019, "username": "glenda" },
    { "id": "h14", "name": "serial-console", "protocol": "serial", "device": "/dev/ttyUSB9",
      "speed": 9600 },
    { "id": "h15", "name": "missing-serial", "protocol": "serial", "device": "/dev/ttyUSB8" }
  ]
}
JSON
/usr/sbin/sshd
pkill -x fake-rexecd || true
/tmp/b/tests/tools/fake-rexecd rexec 5120 alice secret >/tmp/rexecd.log 2>&1 &
/tmp/b/tests/tools/fake-rexecd rsh 5140 alice >/tmp/rshd.log 2>&1 &
# A desktop on display :5, served over VNC by Xvnc and over RDP by
# FreeRDP's shadow server (NTLM login as alice).
pkill -x Xvnc || true; pkill -f freerdp-shadow || true
Xvnc :5 -rfbport 5905 -SecurityTypes None -geometry 1024x768 >/tmp/xvnc.log 2>&1 &
sleep 1
DISPLAY=:5 xterm -geometry 80x24+100+100 >/dev/null 2>&1 &
winpr-hash3 -u alice -p secret -f sam > /tmp/sam
DISPLAY=:5 freerdp-shadow-cli3 /port:3390 /sam-file:/tmp/sam /bind-address:127.0.0.1 >/tmp/shadow.log 2>&1 &
# A serial port whose far end answers like OpenBoot and echoes what it gets.
pkill -x socat || true
socat pty,raw,echo=0,link=/dev/ttyUSB9,mode=666 \
    SYSTEM:'printf "\\r\\nok "; cat' >/tmp/serial.log 2>&1 &
# A mainframe that answers and, eight seconds later, hangs up.
socat TCP-LISTEN:2325,bind=127.0.0.1,reuseaddr,fork SYSTEM:'sleep 8' >/tmp/tn3270.log 2>&1 &
socat TCP-LISTEN:2326,bind=127.0.0.1,reuseaddr,fork SYSTEM:'sleep 8' >/tmp/tn5250.log 2>&1 &
# A Plan 9 CPU server that never answers drawterm's login, then hangs up.
socat TCP-LISTEN:17019,bind=127.0.0.1,reuseaddr,fork SYSTEM:'sleep 8' >/tmp/rcpu.log 2>&1 &
# xdm as an XDMCP host (no local server; queries from anyone).
sed -i 's/^DisplayManager.requestPort/!DisplayManager.requestPort/' /etc/X11/xdm/xdm-config
sed -i 's/^:0 local/#:0 local/' /etc/X11/xdm/Xservers
sed -i 's/^#\*\([[:space:]]*#any host\)/*\1/' /etc/X11/xdm/Xaccess
pgrep -x xdm >/dev/null || xdm
rm -rf /tmp/state  # every run starts from the factory configuration
mkdir -p /tmp/rt /tmp/state /tmp/.X11-unix && chmod 1777 /tmp/.X11-unix
chown pixterm /tmp/rt /tmp/state
chmod 700 /tmp/rt /tmp/state
# The privileged helper (root), as systemd's socket would provide it.
PIXTERM_STATE_DIR=/tmp/state PIXTERM_CONFIG=/tmp/config.json \
    /usr/libexec/pixterm/pixterm-helper --listen /tmp/helper.sock >/tmp/helper.log 2>&1 &
sleep 0.3
su -s /bin/sh pixterm -c 'RUNTIME_DIRECTORY=/tmp/rt STATE_DIRECTORY=/tmp/state PIXTERM_CONFIG=/tmp/config.json PIXTERM_HELPER_SOCKET=/tmp/helper.sock exec /usr/libexec/pixterm/pixtermd' >/tmp/pixtermd.log 2>&1 &
