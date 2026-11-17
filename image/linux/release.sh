#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Make the files of a release from built release images.
#
#   image/linux/release.sh [DIR]
#
# DIR holds what build.sh wrote (default image/linux/out): for each
# architecture, pixterm-VERSION-ARCH.img or .img.xz, its .sha256 and its
# .packages list. The release files go to DIR/release-VERSION:
#
#   pixterm-VERSION-ARCH.img.xz         the image, compressed
#   pixterm-VERSION-ARCH.img.sha256     checksum of the uncompressed image
#   pixterm-VERSION-ARCH.img.packages   every package in the image
#   SHA256SUMS                          checksums of the files above
#   RELEASE-NOTES.md                    the change log's section, and how
#                                       to check and install the image
#   pixterm-imager.json, pixterm-icon.png
#                                       a Raspberry Pi Imager repository,
#                                       when RELEASE_URL is set to where
#                                       the release files will be
#
# The change log must have a section for VERSION (from CMakeLists.txt), or
# a section "Unreleased" when RELEASE_DRAFT=1.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
src=${1:-$here/out}
VERSION=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$root/CMakeLists.txt")
dist=$src/release-$VERSION

# The change log section for this version, as Markdown. AsciiDoc lists
# and monospace text read the same in Markdown; only headings change.
section=$VERSION
[ "${RELEASE_DRAFT:-0}" = 1 ] && section=Unreleased
notes=$(awk -v want="$section" '
	/^== / { inside = ($2 == want); next }
	inside' "$root/CHANGELOG.adoc" |
    sed -e 's/^=== \(.*\)/### \1/')
if [ -z "$(printf '%s' "$notes" | tr -d '[:space:]')" ]; then
	echo "CHANGELOG.adoc has no section \"== $section\"" >&2
	exit 1
fi

rm -rf "$dist"
mkdir -p "$dist"
found=0
for sum in "$src"/pixterm-"$VERSION"-*.img.sha256; do
	[ -f "$sum" ] || continue
	img=${sum%.sha256}
	base=$(basename "$img")
	case $base in *-dev.img) continue ;; esac   # development images are never released
	if [ -f "$img.xz" ]; then
		cp "$img.xz" "$dist/"
	elif [ -f "$img" ]; then
		xz -T0 -9 -c "$img" > "$dist/$base.xz"
	else
		echo "missing $img" >&2
		exit 1
	fi
	cp "$sum" "$img.packages" "$dist/"
	found=$((found + 1))
done
if [ $found = 0 ]; then
	echo "no release images for $VERSION in $src (build with PIXTERM_DEV=0)" >&2
	exit 1
fi

cd "$dist"
sha256() {
	if command -v sha256sum >/dev/null; then sha256sum -- "$@"; else shasum -a 256 -- "$@"; fi
}
# shellcheck disable=SC2035 # sha256 passes "--"; the names start with "pixterm"
sha256 *.img.xz *.img.packages > SHA256SUMS

# Raspberry Pi Imager reads this list ("rpi-imager --repo URL"). Imager's
# own settings screen stays off: pixterm has no cloud-init.
if [ -n "${RELEASE_URL:-}" ]; then
	cp "$root/data/pixterm-icon.png" .
	today=$(date -u +%Y-%m-%d)
	{
		printf '{\n  "os_list": [\n'
		sep=
		for xzf in *.img.xz; do
			arch=${xzf%.img.xz}
			arch=${arch##*-}
			sizes=$(xz --robot --list "$xzf" | awk '$1 == "totals" { print $4, $5 }')
			cat <<ENTRY
$sep    {
      "name": "pixterm $VERSION ($arch)",
      "description": "An X terminal: log in to X11, terminal and XDMCP hosts from a Motif chooser",
      "url": "$RELEASE_URL/$xzf",
      "icon": "$RELEASE_URL/pixterm-icon.png",
      "website": "${RELEASE_SITE:-$RELEASE_URL}",
      "release_date": "$today",
      "extract_size": ${sizes#* },
      "extract_sha256": "$(cut -d' ' -f1 "${xzf%.xz}.sha256")",
      "image_download_size": ${sizes% *},
      "image_download_sha256": "$(sha256 "$xzf" | cut -d' ' -f1)",
      "devices": ["pi5-64bit", "pi4-64bit", "pi3-64bit"]
    }
ENTRY
			sep=,
		done
		printf '  ]\n}\n'
	} > pixterm-imager.json
fi

{
	printf '%s\n\n' "$notes"
	cat <<EOF
### Files

- \`pixterm-$VERSION-arm64.img.xz\`: the SD card image for the Raspberry Pi 3, 4 and 5.
- \`.img.packages\`: every package in the image, with its version.
- \`SHA256SUMS\`: checksums. Check the download with \`sha256sum -c SHA256SUMS\`.

### Installing

Write the image to an SD card with Raspberry Pi Imager ("Use custom"), or
with \`xz -dc pixterm-$VERSION-arm64.img.xz | sudo dd of=/dev/sdX bs=4M\`.

### Upgrading

A new image replaces everything on the card. Before you flash it, save the
settings to a USB drive with Configure > System > Export to USB, and
import them afterwards. The terminal's SSH key is not exported: create a
new one and put it on the hosts that log in with it. The upgrade guide,
\`doc/upgrading.adoc\`, has the details.
EOF
} > RELEASE-NOTES.md

ls -l "$dist"
