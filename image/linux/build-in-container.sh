#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Runs in the privileged builder container: expands the stock image, adds
# the persistent partition, runs customize.sh in a chroot and writes the
# result to /out.
#
# Partition layout of the result:
#   1  boot     FAT, firmware/kernel (read-only at run time)
#   2  root     ext4, the system (read-only at run time)
#   3  persist  ext4 "pixterm-data", configuration (read-only except
#               while pixterm-helper writes a new generation)
set -eu
. /src/image/linux/mount-image.sh

base=$1 out=$2
img=/work/pixterm.img
ROOT_GROW_MB=768
PERSIST_MB=64
mkdir -p /work

echo "=== Unpacking $base"
xz -dc "/src/image/linux/cache/$base.img.xz" > "$img"

echo "=== Partitions: grow root by ${ROOT_GROW_MB} MiB, add ${PERSIST_MB} MiB persist"
start2=$(part_field "$img" 2 start)
size2=$(( $(part_field "$img" 2 size) + ROOT_GROW_MB * 2048 ))
start3=$(( (start2 + size2 + 2047) / 2048 * 2048 ))
size3=$(( PERSIST_MB * 2048 ))
truncate -s $(( (start3 + size3) * 512 )) "$img"
echo "$start2,$size2" | sfdisk --no-reread --quiet -N 2 "$img"
echo "$start3,$size3,L" | sfdisk --no-reread --quiet --append "$img"
loop=$(attach "$img" 2)
e2fsck -fy "$loop" >/dev/null || [ $? -le 1 ]
resize2fs "$loop" >/dev/null
e2label "$loop" rootfs   # fstab mounts by label
losetup -d "$loop"
loop=$(attach "$img" 1)
fatlabel "$loop" bootfs >/dev/null 2>&1 || true
[ "$(blkid -o value -s LABEL "$loop")" = bootfs ] || { echo "boot partition label is not bootfs" >&2; exit 1; }
losetup -d "$loop"
loop=$(attach "$img" 3)
mkfs.ext4 -q -L pixterm-data -m 0 "$loop"
losetup -d "$loop"

mount_image "$img" /mnt
trap 'umount_image /mnt' EXIT
mount --bind /dev /mnt/dev
mount -t devpts devpts /mnt/dev/pts
mount -t proc proc /mnt/proc
mount -t sysfs sys /mnt/sys
rm -f /mnt/etc/resolv.conf
cp /etc/resolv.conf /mnt/etc/resolv.conf

# Downloaded packages are kept on the host, and offered back to apt as a
# local repository so that locked versions stay installable.
mkdir -p /mnt/var/cache/apt/archives /mnt/var/cache/pixterm-debs
mount --bind /debs /mnt/var/cache/apt/archives
mount --bind /debs /mnt/var/cache/pixterm-debs
(cd /debs && apt-ftparchive packages . > Packages 2>/dev/null)

mkdir -p /mnt/tmp/pixterm-src
tar -C /src --exclude=./out --exclude=./image/linux/cache --exclude=./image/linux/out \
    --exclude=./.git -cf - . | tar -C /mnt/tmp/pixterm-src -xf -

chroot /mnt /bin/bash /tmp/pixterm-src/image/linux/customize.sh
cp /mnt/tmp/pixterm-packages "/out/$out.packages"
rm -rf /mnt/tmp/pixterm-src /mnt/tmp/pixterm-packages
umount /mnt/var/cache/pixterm-debs /mnt/var/cache/apt/archives
rmdir /mnt/var/cache/pixterm-debs

umount_image /mnt
trap - EXIT
for n in 2 3; do
	loop=$(attach "$img" $n)
	e2fsck -fy "$loop" >/dev/null || true
	losetup -d "$loop"
done

echo "=== Writing $out"
cp --sparse=always "$img" "/out/$out"
(cd /out && sha256sum "$out" > "$out.sha256")
ls -lh "/out/$out"
