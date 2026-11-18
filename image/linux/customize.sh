#!/bin/bash
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Runs inside the chroot of a stock Raspberry Pi OS Lite image and turns it
# into the pixterm appliance. Invoked by build-in-container.sh.
set -euo pipefail

SRC=/tmp/pixterm-src
DEV=${PIXTERM_DEV:-0}
ARCH=${PIXTERM_ARCH:-$(dpkg --print-architecture)}
LOCK=${PIXTERM_LOCK:-0}
LOCKFILE=$SRC/image/linux/packages.$ARCH.lock
export DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8

RUNTIME_PKGS="xserver-xorg-core xserver-xorg-input-libinput xauth mwm libxm4
	libxext6 libxrandr2 libc++1 libc++abi1 x11-xkb-utils openssh-client
	xfonts-base xfonts-75dpi xfonts-100dpi
	xterm inetutils-telnet tigervnc-viewer freerdp3-x11 picocom
	wpasupplicant wireless-regdb firmware-brcm80211 x3270 xfonts-x3270-misc"
BUILD_PKGS="clang lld cmake ninja-build pkg-config libc++-dev libc++abi-dev libc6-dev
	libmotif-dev libxt-dev libx11-dev libxext-dev libxrandr-dev libssl-dev"
DEV_PKGS="sudo x11-utils x11-xserver-utils strace"

say() { printf '\n=== %s\n' "$*"; }
installed() { dpkg-query -W -f '${db:Status-Abbrev}${binary:Package}\n' "$@" 2>/dev/null | awk '/^ii/{print substr($0, 4)}'; }

# Never start daemons inside the chroot.
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d
chmod 755 /usr/sbin/policy-rc.d

say "Purging packages"
remove=$(grep -v '^#' "$SRC/image/linux/packages-remove.txt")
[ "$DEV" = 1 ] || remove="$remove openssh-server openssh-sftp-server ssh sudo"
# Kernels for boards this architecture does not serve, and all headers.
case $ARCH in
armhf) remove="$remove linux-image-rpi-v6 linux-image-rpi-v8:arm64" ;;
arm64) ;; # rpi-v8 (Pi 3/4) and rpi-2712 (Pi 5) both stay
esac
remove="$remove $(installed 'linux-headers-*')"
# Only names that are installed: purging an unknown package is an error.
# sudo refuses to go while root has no password; release images want no
# root login at all.
# shellcheck disable=SC2046 # package names never hold spaces
SUDO_FORCE_REMOVE=yes apt-get purge -y --auto-remove $(installed $remove)

# Update the kernel and firmware only on purpose, after testing on hardware.
# shellcheck disable=SC2046 # package names never hold spaces
apt-mark hold raspi-firmware $(installed 'linux-image-*') >/dev/null

say "Package sources"
# Every downloaded .deb is kept (build.sh), and offered back as a local
# repository. A locked build pins each package to its recorded version.
echo 'deb [trusted=yes] file:/var/cache/pixterm-debs ./' > /etc/apt/sources.list.d/pixterm-local.list
# The package cache is a directory shared from the build host, which apt's
# unprivileged download user cannot write.
echo 'APT::Sandbox::User "root";' > /etc/apt/apt.conf.d/00pixterm-build
rm -f /etc/apt/preferences.d/pixterm-lock
if [ "$LOCK" != 0 ]; then
	awk -F= '{ printf "Package: %s\nPin: version %s\nPin-Priority: 1001\n\n", $1, $2 }' \
	    "$LOCKFILE" > /etc/apt/preferences.d/pixterm-lock
fi

say "Installing packages"
apt-get update
pkgs="$RUNTIME_PKGS $BUILD_PKGS"
[ "$DEV" = 1 ] && pkgs="$pkgs $DEV_PKGS"
apt-get install -y --no-install-recommends $pkgs
apt-mark manual $RUNTIME_PKGS >/dev/null

say "Building pixterm"
# Only characters that are safe inside a C++ string literal.
BUILD_ID=$(printf '%s' "${PIXTERM_BUILD_ID:-}" | tr -cd 'A-Za-z0-9.+, -')
cd "$SRC"
cmake --preset release -B /tmp/pixterm-build -DCMAKE_INSTALL_PREFIX=/usr \
    -DPIXTERM_BUILD_ID="$BUILD_ID" >/dev/null
cmake --build /tmp/pixterm-build
cmake --install /tmp/pixterm-build --strip >/dev/null
cd /
# rsh connects from a port below 1024; only pixterm-rexec may bind one.
setcap cap_net_bind_service=ep /usr/libexec/pixterm/pixterm-rexec

# The lock records the build tools too, so a locked build uses the same
# compiler.
dpkg-query -W -f '${binary:Package}=${Version}\n' | sort > /tmp/pixterm-build-packages

say "Removing build tools"
# Name the compilers' own packages too: autoremove alone leaves some
# behind. Not libllvm: the X server's Mesa may need it.
# shellcheck disable=SC2046 # package names never hold spaces
apt-get purge -y --auto-remove $(installed $BUILD_PKGS 'clang-[0-9]*' 'lld-[0-9]*' \
    'libclang-cpp[0-9]*' 'libc++-[0-9]*-dev' 'libc++abi-[0-9]*-dev' cmake-data \
    cpp cpp-14 gcc-14 libgcc-14-dev make patch dpkg-dev fakeroot)
# Name resolution: systemd-resolved replaces NetworkManager's resolv.conf.
# Installed last because it re-points /etc/resolv.conf away from the
# chroot's working copy.
apt-get install -y --no-install-recommends systemd-resolved

say "Package manifest"
dpkg-query -W -f '${binary:Package}=${Version}\n' | sort > /tmp/pixterm-packages
sort -u /tmp/pixterm-build-packages /tmp/pixterm-packages > /tmp/pixterm-lock
rm /tmp/pixterm-build-packages
if [ "$LOCK" != 0 ]; then
	# Every installed package must be in the lock at exactly that version.
	# LOCK=add allows packages the lock lacks: they are being added.
	if ! awk -F= -v add="$([ "$LOCK" = add ] && echo 1 || echo 0)" '
	    NR == FNR { v[$1] = $2; next }
	    !($1 in v) { if (add) print "  added: " $0; else { print "  not locked: " $0; bad = 1 }; next }
	    v[$1] != $2 { print "  not locked: " $0; bad = 1 }
	    END { exit bad }' "$LOCKFILE" /tmp/pixterm-packages; then
		echo "installed packages differ from $LOCKFILE" >&2
		exit 1
	fi
fi
rm -f /etc/apt/sources.list.d/pixterm-local.list /etc/apt/preferences.d/pixterm-lock \
    /etc/apt/apt.conf.d/00pixterm-build

say "Users"
id pi >/dev/null 2>&1 && userdel -r pi 2>/dev/null || true
if ! id pixterm >/dev/null 2>&1; then
	useradd --system --user-group --home-dir /run/pixterm/home --no-create-home \
	    --shell /usr/sbin/nologin pixterm
fi
# dialout: the serial ports, for serial console hosts.
usermod -a -G video,input,render,dialout pixterm

if [ "$DEV" = 1 ]; then
	id dev >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo,adm,systemd-journal,video dev
	passwd -l dev >/dev/null
	install -d -m 700 -o dev -g dev /home/dev/.ssh
	install -m 600 -o dev -g dev "$SRC/image/linux/dev/authorized_keys" /home/dev/.ssh/authorized_keys
	# The root filesystem is read-only: host keys are made now, not at boot.
	ssh-keygen -A
fi

say "Configuration files"
cp -a "$SRC/image/linux/rootfs/." /
if [ "$DEV" = 1 ]; then
	cp -a "$SRC/image/linux/rootfs-dev/." /
	chmod 440 /etc/sudoers.d/dev
	# A factory configuration of the builder's own (image/linux/dev is
	# never committed).
	[ -f "$SRC/image/linux/dev/config.json" ] && install -m 644 "$SRC/image/linux/dev/config.json" /etc/pixterm/config.json
fi
chmod 644 /etc/pixterm/config.json
# pixterm-boot.service writes the network configuration into /run at every
# boot, from the pixterm configuration; the file in /usr is only a fallback.
rm -f /etc/systemd/network/*.network
echo pixterm > /etc/hostname
sed -i 's/^127\.0\.1\.1.*/127.0.1.1\tpixterm/' /etc/hosts
grep -q '^127\.0\.1\.1' /etc/hosts || printf '127.0.1.1\tpixterm\n' >> /etc/hosts

say "Read-only root"
# fstab mounts root, boot and the persistent partition read-only; only
# pixterm-helper makes the persistent partition writable, and only briefly.
# What the system must write at run time goes to RAM.
cat > /etc/fstab <<'EOF'
# pixterm: read-only appliance. Configuration lives on "pixterm-data".
proc                /proc               proc   defaults                              0 0
LABEL=bootfs        /boot/firmware      vfat   ro,defaults                           0 2
LABEL=rootfs        /                   ext4   ro,defaults,noatime                   0 1
LABEL=pixterm-data  /var/lib/pixterm    ext4   ro,noatime,nodev,nosuid,noexec        0 2
tmpfs               /var/lib/systemd    tmpfs  mode=0755,nosuid,nodev,size=8m        0 0
tmpfs               /var/log            tmpfs  mode=0755,nosuid,nodev,size=16m       0 0
tmpfs               /var/tmp            tmpfs  mode=1777,nosuid,nodev,size=16m       0 0
EOF
install -d -m 755 /var/lib/pixterm
# An empty machine-id: systemd makes a transient one at every boot.
: > /etc/machine-id
rm -f /var/lib/dbus/machine-id

say "Services"
systemctl enable systemd-networkd.service systemd-resolved.service \
    systemd-timesyncd.service pixtermd.service pixterm-boot.service pixterm-helper.socket
systemctl set-default graphical.target
unwanted="getty@tty1.service serial-getty@ttyS0.service serial-getty@ttyAMA0.service
    systemd-networkd-wait-online.service
    apt-daily.timer apt-daily-upgrade.timer dpkg-db-backup.timer man-db.timer
    e2scrub_all.timer e2scrub_reap.service fstrim.timer
    systemd-pstore.service systemd-rfkill.service systemd-rfkill.socket
    wpa_supplicant.service
    keyboard-setup.service console-setup.service sshswitch.service
    alsa-restore.service alsa-state.service
    regenerate_ssh_host_keys.service sshd-keygen.service systemd-growfs-root.service
    user@.service"
# user@.service: pixtermd's logind session needs no per-user systemd (13 MB).
systemctl disable $unwanted 2>/dev/null || true
systemctl mask $unwanted
systemctl disable remote-fs.target
# Wi-Fi: wpa_supplicant reads the configuration pixterm-helper writes to
# /run; without one it does not start (see its drop-in in rootfs).
install -d -m 755 /etc/wpa_supplicant
ln -sf /run/pixterm-wifi/wpa_supplicant.conf /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
systemctl enable wpa_supplicant@wlan0.service
if [ "$DEV" = 1 ]; then
	systemctl enable ssh.service pixterm-debuglog.service
fi

say "Boot configuration"
cfg=/boot/firmware/config.txt
sed -i -e 's/^dtparam=audio=on/dtparam=audio=off/' \
       -e 's/^camera_auto_detect=1/camera_auto_detect=0/' "$cfg"
cat >> "$cfg" <<'EOF'

[all]
# pixterm appliance: no Bluetooth, no rainbow splash.
dtoverlay=disable-bt
disable_splash=1
EOF
cmd=/boot/firmware/cmdline.txt
# No first-boot resize: the persistent partition follows the root.
sed -i -e 's/console=serial0,115200 //' -e 's/ resize\b//' -e 's/ init=[^ ]*//' "$cmd"
if [ "$DEV" = 1 ]; then
	# Keep boot messages visible on development images.
	sed -i -e 's/$/ logo.nologo/' "$cmd"
else
	sed -i -e 's/$/ quiet loglevel=3 logo.nologo/' "$cmd"
fi
rm -f /boot/firmware/user-data /boot/firmware/network-config /boot/firmware/meta-data

say "Cleaning up"
# Not apt-get clean: /var/cache/apt/archives is the host's package cache
# here (build-in-container.sh), and the image's own copy is empty.
rm -f /var/cache/apt/*.bin
rm -rf /var/lib/apt/lists/* /var/cache/debconf/*-old /var/log/* /tmp/pixterm-build
rm -f /usr/sbin/policy-rc.d
rm -f /etc/resolv.conf
ln -s ../run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
# Mark /etc and /var as up to date with /usr, so ConditionNeedsUpdate units
# (ldconfig, systemd-update-done) never try to write the read-only root.
touch /etc/.updated /var/.updated

say "Enabled units"
systemctl list-unit-files --state=enabled --no-legend | awk '{print $1}' | sort | tr '\n' ' '
echo
