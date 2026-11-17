#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Write a pixterm image to an SD card on macOS.
#
#   sudo image/linux/flash.sh [-y] /dev/diskN [image]
#
# Refuses anything that does not look like removable media. Without -y it
# shows the target and asks for confirmation.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
yes=0
[ "${1:-}" = -y ] && { yes=1; shift; }
disk=${1:-}
img=${2:-$(ls -t "$here"/out/*.img 2>/dev/null | head -1)}

case $disk in
/dev/disk[0-9]|/dev/disk[0-9][0-9]) ;;
*) echo "usage: $0 [-y] /dev/diskN [image]" >&2; exit 2 ;;
esac
[ -f "$img" ] || { echo "no image found (run image/linux/build.sh)" >&2; exit 1; }

info=$(diskutil info "$disk") || exit 1
if ! printf '%s\n' "$info" | grep -Eq 'Removable Media: +(Removable|Yes)|Device Location: +External'; then
	echo "$disk is not removable/external media; refusing." >&2
	exit 1
fi
if printf '%s\n' "$info" | grep -Eq 'Virtual: +Yes|Protocol: +Disk Image'; then
	echo "$disk is a disk image, not an SD card; refusing." >&2
	exit 1
fi
bytes=$(printf '%s\n' "$info" | sed -n 's/.*Disk Size:.*(\([0-9]*\) Bytes).*/\1/p')
if [ -z "$bytes" ] || [ "$bytes" -gt $((256 * 1000 * 1000 * 1000)) ]; then
	echo "$disk is larger than 256 GB (or its size is unknown); refusing." >&2
	exit 1
fi

printf '%s\n' "$info" | grep -E 'Device / Media Name|Disk Size|Device Location|Removable Media|Protocol'
echo "Image: $img ($(du -h "$img" | cut -f1))"
if [ "$yes" != 1 ]; then
	printf 'ERASE %s and write the image? Type yes: ' "$disk"
	read -r answer
	[ "$answer" = yes ] || { echo "aborted"; exit 1; }
fi

[ "$(id -u)" = 0 ] || { echo "writing requires root: run with sudo" >&2; exit 1; }
diskutil unmountDisk "$disk"
dd if="$img" of="/dev/r${disk#/dev/}" bs=4m status=progress
sync
diskutil eject "$disk"
echo "Done. Put the card in the Pi and switch it on."
