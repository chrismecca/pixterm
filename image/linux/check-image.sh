#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Checks a built image's root file system for the promise that release
# images run no network services. build-in-container.sh runs it on every
# image.
#
#   check-image.sh ROOT 0|1     ROOT: the mounted root; 1 for a development image
#
# Every unit enabled in /etc must be one pixterm means to run, none of
# which listens on the network; a release image has no SSH server; and
# systemd-resolved answers no one.
set -eu

root=$1
dev=$2

# Clients of the network (DHCP, NTP, DNS, Wi-Fi), pixterm's own units
# (pixterm-helper's socket is a Unix one), and the first-boot resize.
allowed="pixterm-boot.service pixterm-helper.socket pixtermd.service
	systemd-networkd.service systemd-networkd.socket systemd-network-generator.service
	systemd-resolved.service systemd-timesyncd.service wpa_supplicant@wlan0.service
	rpi-resize.service"
[ "$dev" = 1 ] && allowed="$allowed ssh.service pixterm-debuglog.service"

bad=0
problem() {
	echo "check-image: $*" >&2
	bad=1
}

for link in "$root"/etc/systemd/system/*.wants/*; do
	[ -L "$link" ] || [ -e "$link" ] || continue
	unit=${link##*/}
	case " $(echo $allowed) " in
	*" $unit "*) ;;
	*) problem "$unit is enabled, but not one pixterm means to run" ;;
	esac
done

if [ "$dev" != 1 ]; then
	for sshd in "$root/usr/sbin/sshd" "$root/usr/lib/openssh/sshd-session"; do
		[ -e "$sshd" ] && problem "a release image has an SSH server: ${sshd#"$root"}"
	done
fi

resolved=$root/etc/systemd/resolved.conf.d/pixterm.conf
for setting in LLMNR=no MulticastDNS=no; do
	grep -qx "$setting" "$resolved" 2>/dev/null ||
		problem "systemd-resolved answers on the network: no $setting in ${resolved#"$root"}"
done

[ $bad = 0 ] && echo "check-image: no network services"
exit $bad
