#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Shared helpers for attaching a Raspberry Pi OS image inside the builder
# container. Partition device nodes are not reliably created inside
# containers, so each partition gets its own loop device, with --offset.

# part_field IMG N FIELD -> start|size (in 512-byte sectors)
part_field() {
	sfdisk -d "$1" | awk -v n="$2" -v f="$3" '
		$0 ~ "^[^ ]*" n " : " {
			for (i = 1; i <= NF; i++)
				if ($i ~ "^" f "=") { v = $(i); sub("^" f "=", "", v); sub(",", "", v)
					if (v == "") v = $(i + 1); sub(",", "", v); print v }
		}'
}

# attach IMG N -> prints loop device for partition N
attach() {
	_start=$(part_field "$1" "$2" start)
	_size=$(part_field "$1" "$2" size)
	losetup -f --show -o $((_start * 512)) --sizelimit $((_size * 512)) "$1"
}

# mount_image IMG MNT [ro]: use "ro" when only inspecting, so the image
# (and its checksum) stays unchanged.
mount_image() {
	img=$1 mnt=$2 opts=${3:-rw}
	ROOT_LOOP=$(attach "$img" 2)
	BOOT_LOOP=$(attach "$img" 1)
	mkdir -p "$mnt"
	[ "$opts" = ro ] && opts=ro,noload
	mount -o "$opts" "$ROOT_LOOP" "$mnt"
	mount -o "${opts%%,*}" "$BOOT_LOOP" "$mnt/boot/firmware"
	export ROOT_LOOP BOOT_LOOP
}

umount_image() {
	mnt=$1
	for m in var/cache/pixterm-debs var/cache/apt/archives dev/pts dev proc sys boot/firmware; do
		mountpoint -q "$mnt/$m" && umount "$mnt/$m"
	done
	mountpoint -q "$mnt" && umount "$mnt"
	[ -n "${BOOT_LOOP:-}" ] && losetup -d "$BOOT_LOOP"
	[ -n "${ROOT_LOOP:-}" ] && losetup -d "$ROOT_LOOP"
	return 0
}
