#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# The session test: drives the chooser with xdotool through a session of
# each kind and checks pixtermd's log. Run inside the session test
# container after setup.sh.
set -u
T=/src/tests/integration
XDO=$T/x
LOG=/tmp/pixtermd.log
fails=0

step() { printf '\n== %s\n' "$*"; }
ok()   { printf 'ok   %s\n' "$*"; }
bad()  { printf 'FAIL %s\n' "$*"; fails=$((fails + 1)); }

# wait_log PATTERN [SECONDS]: wait until the log (since mark) matches
mark() { MARK=$(wc -l < "$LOG"); }
wait_log() {
	i=0
	while [ $i -lt $(( ${2:-10} * 5 )) ]; do
		tail -n +"$((MARK + 1))" "$LOG" | grep -q -- "$1" && { ok "log: $1"; return 0; }
		sleep 0.2; i=$((i + 1))
	done
	bad "log: $1"; tail -n +"$((MARK + 1))" "$LOG" | sed 's/^/     | /'; return 1
}
# wait_x --name|--classname PATTERN [SECONDS]: wait until a window matches.
wait_x() {
	end=$(( $(date +%s) + ${3:-10} ))
	until $T/search "$1" "$2" >/dev/null; do
		[ "$(date +%s)" -lt $end ] || return 1
		sleep 0.2
	done
}
wait_win() { wait_x --name "$1" "${2:-10}" && ok "window: $1" || bad "window: $1"; }
wait_class() { wait_x --classname "$1" "${2:-10}" && ok "dialog: $1" || bad "dialog: $1"; }

# Click the left ("Yes") button of the askpass question dialog.
click_yes() {
	w=$($T/search --name "Login to" | head -1)
	eval "$($XDO xdotool getwindowgeometry --shell "$w")"
	$XDO xdotool mousemove $((X + 40)) $((Y + HEIGHT - 33)) click 1
}
password() { wait_win "Login to" && sleep 0.5 && $XDO xdotool type --delay 20 "$1" && $XDO xdotool key Return; }
shot() { $T/shot "$1"; }
# pick N: open the Nth host after the first. Keys pressed quickly while the
# list scrolls can be lost, so press them one at a time.
pick() {
	$XDO xdotool key ctrl+Home; sleep 0.2
	n=$1
	while [ "$n" -gt 0 ]; do $XDO xdotool key Down; sleep 0.1; n=$((n - 1)); done
	sleep 0.3; $XDO xdotool key Return
}
# pick_end N: open the Nth host before the last; fewer keys for hosts near the end.
pick_end() {
	$XDO xdotool key ctrl+End; sleep 0.2
	n=$1
	while [ "$n" -gt 0 ]; do $XDO xdotool key Up; sleep 0.1; n=$((n - 1)); done
	sleep 0.3; $XDO xdotool key Return
}

rm -f /tmp/state/known_hosts
i=0; until $XDO xdpyinfo >/dev/null 2>&1 || [ $i -ge 30 ]; do sleep 0.5; i=$((i + 1)); done
wait_win "pixterm - Network Terminal" 15
sleep 1

# Xvfb has no DPMS; the unit tests check the arguments that set it.
step "the screen blanks after 10 minutes"
saver=$($XDO xset q)
echo "$saver" | grep -Eq 'timeout: +600 ' && ok "screen saver: 600 s" || bad "screen saver: $saver"

# Xvfb's one output, turned off, stands in for a monitor plugged in
# after Xorg started: connected, but dark.
step "the chooser lights a dark monitor"
$XDO xrandr --output screen --off 2>/dev/null
$XDO xrandr | grep -q '\*' && bad "output turned off" || ok "output turned off"
sleep 2
$XDO xrandr | grep -q '1280x1024 .*\*' && ok "output lit at 1280x1024" || bad "output lit at 1280x1024"

step "ask-for-user: username prompt, host key, password, session"
mark
$XDO xdotool key Down Return
wait_class userDialog_popup
sleep 0.5; shot s1-username
$XDO xdotool type --delay 20 alice; $XDO xdotool key Return
wait_win "Login to ask-for-user"
sleep 0.5; shot s2-hostkey
click_yes
sleep 1
password secret
wait_log "state handoff -> session"
sleep 1; shot s3-session
ps -eo user,args | grep -q '^alice *xterm' && ok "remote xterm running" || bad "remote xterm running"

step "display reset during session (Ctrl+Alt+Backspace equivalent)"
mark
pkill -x Xvfb
wait_log "ended by a display reset"
wait_log "started pixterm-ui" 15
wait_class errorDialog_popup
sleep 0.5; shot s4-reset-error
ps -eo user,args | grep -q '^alice *xterm' && bad "remote xterm gone" || ok "remote xterm gone"
[ ! -e /tmp/rt/ssh-control ] && ok "control socket removed" || bad "control socket removed"
$XDO xdotool key Return   # dismiss
sleep 0.5

step "unreachable host: error dialog after connect timeout"
mark
pick 2
wait_log "state local -> connecting"
sleep 1; shot s5-connecting
wait_log "Could not log in" 25
wait_class errorDialog_popup
sleep 0.5; shot s6-unreachable
$XDO xdotool key Return
sleep 0.5

step "silent XDMCP host: error on the chooser, display untouched"
mark
pick 3
wait_log "state local -> probe"
wait_log "did not answer the XDMCP query" 15
wait_class errorDialog_popup
sleep 0.5; shot s6b-xdmcp-silent
grep -q "x_switch" /tmp/pixtermd.log && bad "display was not switched" || ok "display was not switched"
$XDO xdotool key Return
sleep 0.5

step "normal logout returns to chooser"
mark
$XDO xdotool key ctrl+Home Return
password secret
wait_log "state handoff -> session"
sleep 6   # a session shorter than 5 s counts as one that failed to start
$XDO xdotool mousemove 100 100; sleep 0.3
$XDO xdotool type --delay 20 exit; $XDO xdotool key Return
wait_log "session on localhost-xterm ended"
wait_log "started pixterm-ui" 15
sleep 1; shot s7-after-logout

step "connection lost during session"
mark
$XDO xdotool key Return
password secret
wait_log "state handoff -> session"
sleep 1
pkill -f 'sshd-session: alice'
wait_log "was lost" 15
wait_log "started pixterm-ui" 15
wait_class errorDialog_popup
sleep 0.5; shot s8-lost
$XDO xdotool key Return

step "Find Hosts: discover xdm, log in over XDMCP, log out"
mark
$T/click-rel "pixterm - Network Terminal" 50 83
wait_win "Find XDMCP Hosts"
sleep 5; shot s9-find
$T/click-rel "Find XDMCP Hosts" 7 25   # Connect
wait_log "is willing"
wait_log "state x_setup -> remote" 15
sleep 4; shot s10-greeter
$XDO xdotool type --delay 30 alice; $XDO xdotool key Return; sleep 1
$XDO xdotool type --delay 30 secret; $XDO xdotool key Return
i=0; until ps -eo user,args | grep -q '^alice *xterm' || [ $i -ge 30 ]; do sleep 0.5; i=$((i + 1)); done
ps -eo user,args | grep -q '^alice *xterm' && ok "XDMCP session xterm running" || bad "XDMCP session xterm running"
sleep 6; pkill -u alice -x xterm
wait_log "XDMCP session on .* ended" 20
wait_log "started pixterm-ui" 15
sleep 1

step "administrator PIN guards Configure"
mark
$T/click-rel "pixterm - Network Terminal" 82 83
wait_win "pixterm Configuration"
$T/click-rel "pixterm Configuration" 7 393     # Wi-Fi
sleep 1; shot s10b-wifi
$T/click-rel "pixterm Configuration" 7 355     # System
sleep 1
$T/click-rel "pixterm Configuration" 84 136    # Administrator PIN...
wait_win "Administrator PIN"
$XDO xdotool type --delay 30 1234; $XDO xdotool key Tab; $XDO xdotool type --delay 30 1234; $XDO xdotool key Return
wait_class infoDialog_popup
grep -q '"admin_pin":' /tmp/state/config.json && ok "PIN hash saved" || bad "PIN hash saved"
grep -q 1234 /tmp/state/config.json && bad "PIN stored in clear" || ok "PIN not stored in clear"
$XDO xdotool key Return; sleep 0.5
$T/click-rel "pixterm Configuration" 95 25   # Close
sleep 0.5
pkill -u pixterm -x pixterm-ui   # a new chooser is locked again
wait_log "started pixterm-ui" 10
sleep 2
$T/click-rel "pixterm - Network Terminal" 82 83
wait_class pinDialog_popup
$XDO xdotool type --delay 30 9999; $XDO xdotool key Return
wait_class errorDialog_popup
$XDO xdotool key Return; sleep 0.5
$XDO xdotool type --delay 30 1234; $XDO xdotool key Return
wait_win "pixterm Configuration" 15
sleep 1; shot s11-unlocked
$T/click-rel "pixterm Configuration" 95 25   # Close
sleep 1

step "key login: no key yet, then the terminal's key"
mark
$T/click-rel "pixterm - Network Terminal" 12 367   # alice-by-key
$XDO xdotool key Return
wait_log "has no key yet"
wait_class errorDialog_popup
$XDO xdotool key Return; sleep 0.5
su -s /bin/sh pixterm -c '/tmp/helper-call /tmp/helper.sock keygen' > /tmp/key.out
grep -q '^OK' /tmp/key.out && ok "helper made a key" || bad "helper made a key"
[ "$(stat -c '%U %a' /tmp/state/id_ed25519)" = "pixterm 600" ] && ok "private key 600 pixterm" || bad "private key 600 pixterm"
install -d -m 700 -o alice -g alice /home/alice/.ssh
install -m 600 -o alice -g alice /tmp/state/id_ed25519.pub /home/alice/.ssh/authorized_keys
mark
$T/click-rel "pixterm - Network Terminal" 12 367
$XDO xdotool key Return
wait_log "state handoff -> session" 15
$T/search --name "Login to" >/dev/null && bad "no password prompt" || ok "no password prompt"
sleep 6; pkill -u alice -x xterm
wait_log "session on alice-by-key ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "SSH terminal: log in, use the remote shell, exit"
mark
pick 5
password secret
wait_log "state handoff -> session" 15
wait_log "terminal for alice-terminal"
ps -eo user,args | grep -q '^pixterm */usr/bin/xterm .*-e /usr/bin/ssh .*-tt' && ok "terminal runs ssh" || bad "terminal runs ssh"
sleep 6; shot s12-terminal
$XDO xdotool mousemove 100 100; sleep 0.3
$XDO xdotool type --delay 20 exit; $XDO xdotool key Return
wait_log "terminal session on alice-terminal ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "Telnet: a closed port is reported on the chooser"
mark
pick 7
wait_log "state local -> probe"
wait_log "Could not connect to closed-telnet" 15
wait_class errorDialog_popup
sleep 0.5; shot s13-telnet-refused
tail -n +"$((MARK + 1))" "$LOG" | grep -q HANDOFF && bad "chooser kept the display" || ok "chooser kept the display"
$XDO xdotool key Return
sleep 0.5

step "Telnet: a terminal session until the host closes it"
sleep 3600 | nc -lk 127.0.0.1 2323 >/dev/null 2>&1 &
sleep 0.5
mark
pick 6
wait_log "state handoff -> session" 15
ps -eo user,args | grep -q '^pixterm */usr/bin/xterm .*-e /usr/bin/telnet -8 -E 127.0.0.1 2323' && ok "terminal runs telnet" || bad "terminal runs telnet"
sleep 6
pkill -x nc; pkill -x -f 'sleep 3600'
wait_log "terminal session on fake-telnet ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "Direct X: the host opens a window, then closes it"
mark
pick 8
wait_log "waiting for windows from direct-x-host" 20
wait_log "started pixterm-notice"
sleep 1; shot s14-waiting
runuser -u alice -- xclock -display 127.0.0.1:0 >/dev/null 2>&1 &
wait_log "direct-x-host opened a window" 10
sleep 1; shot s15-direct-x
pkill -x xclock
wait_log "direct X session on direct-x-host ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "rexec: a wrong password is refused; the right one runs the command"
mark
pick 9
password wrong
wait_log "refused the command" 25
wait_log "started pixterm-ui" 15
wait_class errorDialog_popup
$XDO xdotool key Return; sleep 0.5
mark
pick 9
password secret
wait_log "running \"xterm -display 127.0.0.1:0\" on rexec-host" 25
i=0; until ps -eo user,args | grep -q '^alice *xterm -display 127.0.0.1:0' || [ $i -ge 20 ]; do sleep 0.5; i=$((i + 1)); done
ps -eo user,args | grep -q '^alice *xterm -display 127.0.0.1:0' && ok "rexec command running" || bad "rexec command running"
grep -q secret /tmp/pixtermd.log && bad "password kept out of the log" || ok "password kept out of the log"
sleep 6; shot s16-rexec
pkill -u alice -x xterm
wait_log "command on rexec-host ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "rsh: the command runs, from a reserved port"
mark
pick 10
wait_log "running \"xterm -display 127.0.0.1:0\" on rsh-host" 25
i=0; until ps -eo user,args | grep -q '^alice *xterm -display 127.0.0.1:0' || [ $i -ge 20 ]; do sleep 0.5; i=$((i + 1)); done
ps -eo user,args | grep -q '^alice *xterm -display 127.0.0.1:0' && ok "rsh command running" || bad "rsh command running"
sleep 6
pkill -u alice -x xterm
wait_log "command on rsh-host ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "RDP: a wrong password is refused on the chooser; the right one shows the desktop"
mark
pick 12
password wrong
wait_log "The user name or password is wrong" 30
wait_class errorDialog_popup
tail -n +"$((MARK + 1))" "$LOG" | grep -q HANDOFF && bad "chooser kept the display" || ok "chooser kept the display"
$XDO xdotool key Return; sleep 0.5
mark
pick 12
password secret
wait_log "state handoff -> session" 30
ps -eo user,args | grep -q '^pixterm */usr/bin/xfreerdp3 .*/f' && ok "FreeRDP running" || bad "FreeRDP running"
ps -u pixterm -o args | grep -q secret && bad "password kept off command lines" || ok "password kept off command lines"
grep -q secret /tmp/pixtermd.log && bad "password kept out of the log" || ok "password kept out of the log"
wait_log "RDP certificate saved"
grep -q "BEGIN CERTIFICATE" /tmp/state/rdp-certs/127.0.0.1_3390.pem && ok "certificate kept" || bad "certificate kept"
sleep 6; shot s17-rdp
pkill -x xfreerdp3
wait_log "on rdp-host ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "VNC: the desktop fills the screen"
mark
pick 11
wait_log "state handoff -> session" 20
ps -eo user,args | grep -q '^pixterm */usr/bin/xtigervncviewer -geometry 1280x1024+0+0' && ok "viewer fills the screen" || bad "viewer fills the screen"
sleep 6; shot s18-vnc
pkill -x xtigervncviewer
wait_log "on vnc-host ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "Serial console: a missing port is reported; a present one opens"
mark
pick_end 0
wait_log "There is no serial port /dev/ttyUSB8"
wait_class errorDialog_popup
$XDO xdotool key Return; sleep 0.5
mark
pick_end 1
wait_log "state handoff -> session" 15
ps -eo user,args | grep -q '^pixterm */usr/bin/xterm .*-e /usr/bin/picocom .*--send-cmd  --receive-cmd  /dev/ttyUSB9' && ok "terminal runs picocom" || bad "terminal runs picocom"
sleep 6; shot s19-serial
$XDO xdotool mousemove 100 100; sleep 0.3
# Keys sent while xterm is still settling can be lost: send them again
# while picocom runs.
for _ in 1 2 3; do
	$XDO xdotool key --delay 150 ctrl+a ctrl+x
	sleep 3
	pgrep -x picocom >/dev/null || break
done
wait_log "terminal session on serial-console ended" 15
wait_log "started pixterm-ui" 15
sleep 1

step "leftover processes"
sleep 1
left=$(ps -eo user,args | grep -E '^(alice|pixterm) ' | grep -vE 'pixtermd|Xvfb|mwm|pixterm-ui|ps -eo|grep|ssh-agent' || true)
[ -z "$left" ] && ok "only pixtermd, Xvfb, mwm, pixterm-ui remain" || { bad "leftover processes"; echo "$left"; }

printf '\n%d failure(s)\n' "$fails"
exit $fails
