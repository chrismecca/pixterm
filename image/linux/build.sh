#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Build the pixterm SD card image on macOS (or Linux) with Docker.
#
#   image/linux/build.sh                    development image, arm64
#   PIXTERM_DEV=0 image/linux/build.sh      release image (no inbound services)
#   PIXTERM_ARCH=armhf image/linux/build.sh 32-bit image (Pi 2/3 and later)
#   PIXTERM_LOCK=1 image/linux/build.sh     reproduce packages.<arch>.lock exactly
#   PIXTERM_LOCK=add image/linux/build.sh   keep the locked versions, add packages
#                                           new to the image to the lock (build a
#                                           development image: it has the most)
#   PIXTERM_DEV_KEYS=FILE image/linux/build.sh
#                                           also let the SSH public keys in FILE
#                                           log in to a development image
#
# The script downloads the stock Raspberry Pi OS Lite image once into
# cache/ and checks it. It keeps every .deb the build installs in
# cache/debs-<arch>/, so a locked build still works after the archive has
# moved on. It writes the image, its checksum and its package list to out/.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
RELEASE=2026-10-06
ARCH=${PIXTERM_ARCH:-arm64}
DEV=${PIXTERM_DEV:-1}
LOCK=${PIXTERM_LOCK:-0}
case $ARCH in
arm64) PLATFORM=linux/arm64 ;;
armhf) PLATFORM=linux/arm/v7 ;;
*) echo "PIXTERM_ARCH must be arm64 or armhf" >&2; exit 2 ;;
esac
BASE=$RELEASE-raspios-trixie-$ARCH-lite
URL=https://downloads.raspberrypi.com/raspios_lite_$ARCH/images/raspios_lite_$ARCH-$RELEASE/$BASE.img.xz
VERSION=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$root/CMakeLists.txt")
SUFFIX=
[ "$DEV" = 1 ] && SUFFIX=-dev
OUT=pixterm-$VERSION-$ARCH$SUFFIX.img

mkdir -p "$here/cache/debs-$ARCH" "$here/out"
cd "$here/cache"
if [ ! -f "$BASE.img.xz" ]; then
	curl -fL -o "$BASE.img.xz.part" "$URL" && mv "$BASE.img.xz.part" "$BASE.img.xz"
	curl -fsSLO "$URL.sha256"
fi
shasum -a 256 -c "$BASE.img.xz.sha256"
# Programs built from upstream source (sources.txt), each checked.
"$here/third-party.sh" fetch "$here/cache/sources"

case $LOCK in
0) ;;
1|add)
	if [ "$LOCK" = add ] && [ "$DEV" != 1 ]; then
		echo "PIXTERM_LOCK=add needs a development image (PIXTERM_DEV=1): it has every package" >&2
		exit 2
	fi
	if [ ! -f "$here/packages.$ARCH.lock" ]; then
		echo "PIXTERM_LOCK=$LOCK needs image/linux/packages.$ARCH.lock (from an earlier build)" >&2
		exit 1
	fi ;;
*) echo "PIXTERM_LOCK must be 0, 1 or add" >&2; exit 2 ;;
esac

# Foreign binaries in the chroot run through qemu-user (binfmt_misc)
# (arm64 runs natively on Apple Silicon).
if ! docker run --rm --platform "$PLATFORM" debian:trixie-slim true 2>/dev/null; then
	docker run --privileged --rm tonistiigi/binfmt --install arm,arm64
fi

if [ "$DEV" = 1 ]; then
	dev="$here/dev"
	mkdir -p "$dev"
	[ -f "$dev/pixterm_dev_ed25519" ] ||
		ssh-keygen -q -t ed25519 -N '' -C pixterm-dev -f "$dev/pixterm_dev_ed25519"
	# The development key, and any the builder names.
	cat "$dev/pixterm_dev_ed25519.pub" ${PIXTERM_DEV_KEYS:+"$PIXTERM_DEV_KEYS"} \
	    > "$dev/authorized_keys"
fi

docker build -q -t pixterm-builder -f "$here/Dockerfile.builder" "$here" >/dev/null
# The build identifier shown in Configure: the commit, "+" if the work tree
# had changes, and whether this is a development image.
BUILD_ID=$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)
git -C "$root" diff --quiet HEAD 2>/dev/null || BUILD_ID="$BUILD_ID+"
[ "$DEV" = 1 ] && BUILD_ID="$BUILD_ID, development"

docker run --rm --privileged -v /dev:/dev \
    -v "$root":/src:ro -v "$here/out":/out -v "$here/cache/debs-$ARCH":/debs \
    -e PIXTERM_DEV="$DEV" -e PIXTERM_ARCH="$ARCH" -e PIXTERM_LOCK="$LOCK" \
    -e PIXTERM_BUILD_ID="$BUILD_ID" -e PIXTERM_OWNER="$(id -u):$(id -g)" \
    pixterm-builder /src/image/linux/build-in-container.sh "$BASE" "$OUT"

# Every build leaves the versions it installed, the build tools' among
# them, in out/. The first build of an architecture records them as its
# lock, and PIXTERM_LOCK=add records the added ones. To move every
# package to its newest version, build with PIXTERM_LOCK=0 and copy the
# new lock over the old one.
if [ ! -f "$here/packages.$ARCH.lock" ] || [ "$LOCK" = add ]; then
	cp "$here/out/$OUT.lock" "$here/packages.$ARCH.lock"
fi
