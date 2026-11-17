#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Builds pixterm for a Pi running a development image and installs it
# there without reflashing: the programs, the data files and the systemd
# units, then restarts pixtermd. The read-only root is made writable only
# while installing.
#
#   PIXTERM_PI=ADDRESS image/linux/deploy-dev.sh
#
# The Pi must accept the development key that build.sh made.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
pi=${PIXTERM_PI:?set PIXTERM_PI to the address of the Pi}
key=$here/dev/pixterm_dev_ed25519
on_pi() {
	ssh -i "$key" -o IdentitiesOnly=yes -o UserKnownHostsFile="$here/dev/known_hosts" \
	    "dev@$pi" "$@"
}

arch=$(on_pi dpkg --print-architecture)
case $arch in
arm64) platform=linux/arm64 ;;
armhf) platform=linux/arm/v7 ;;
*) echo "unsupported architecture: $arch" >&2; exit 1 ;;
esac

# The toolchain container, for the Pi's architecture: the same Debian
# release as the image.
docker build -q --platform $platform -t "pixterm-ci-$arch" "$root/tools/ci" >/dev/null
stage=out/deploy-$arch
mkdir -p "$root/out"
docker run --rm --platform $platform -v "$root":/src -w /src "pixterm-ci-$arch" sh -ec "
	cmake --preset release -B /tmp/b -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
	cmake --build /tmp/b >/dev/null
	rm -rf /tmp/stage
	DESTDIR=/tmp/stage cmake --install /tmp/b >/dev/null
	tar -C /tmp/stage -cf /src/$stage.tar ."

on_pi 'set -e
ro=0
findmnt -no OPTIONS / | grep -q "^ro" && { ro=1; sudo mount -o remount,rw /; }
sudo tar -C / --no-same-owner --no-overwrite-dir --keep-directory-symlink -xf -
sudo setcap cap_net_bind_service=ep /usr/libexec/pixterm/pixterm-rexec
sudo systemctl daemon-reload
sudo systemctl enable --now pixterm-helper.socket
sudo systemctl enable pixterm-boot.service
# Restart first: Linux refuses a read-only remount while a running
# program still holds a replaced (deleted) binary open.
sudo systemctl restart pixtermd
sleep 2
sync
if [ $ro = 1 ] && ! sudo mount -o remount,ro /; then
	echo "warning: / stays writable until the next reboot" >&2
fi
echo "deployed; pixtermd restarted"' <"$root/$stage.tar"
