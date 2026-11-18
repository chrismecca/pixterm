#!/bin/sh
# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# The programs in sources.txt, which pixterm builds from upstream source.
#
#   third-party.sh fetch DIR            download each into DIR and check it
#   third-party.sh build DIR NAME PREFIX
#                                       build NAME from DIR, install under PREFIX
#
# A tarball must match its SHA-256 and a git checkout its commit; a
# source that does not is refused, and nothing is built from it.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
sources=$here/sources.txt

sha256() {
	if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

entries() { grep -v '^[[:space:]]*\(#\|$\)' "$sources"; }

fetch() {
	dir=$1
	mkdir -p "$dir"
	entries | while read -r kind name url pin; do
		case $kind in
		tarball)
			file=$dir/$name.tar.gz
			if [ ! -f "$file" ] || [ "$(sha256 "$file")" != "$pin" ]; then
				curl -fsSL -o "$file.part" "$url"
				mv "$file.part" "$file"
			fi
			if [ "$(sha256 "$file")" != "$pin" ]; then
				echo "$name: $url does not match its SHA-256" >&2
				rm -f "$file"
				exit 1
			fi
			;;
		git)
			repo=$dir/$name
			if [ ! -d "$repo/.git" ]; then
				git init -q "$repo"
				git -C "$repo" remote add origin "$url"
			fi
			if [ "$(git -C "$repo" rev-parse -q --verify HEAD 2>/dev/null)" != "$pin" ]; then
				git -C "$repo" fetch -q --depth 1 origin "$pin"
				git -C "$repo" -c advice.detachedHead=false checkout -q --force FETCH_HEAD
			fi
			if [ "$(git -C "$repo" rev-parse HEAD)" != "$pin" ]; then
				echo "$name: $url did not give commit $pin" >&2
				exit 1
			fi
			;;
		*)
			echo "sources.txt: unknown kind $kind" >&2
			exit 1
			;;
		esac
	done
}

build() {
	dir=$1 name=$2 prefix=$3
	work=$(mktemp -d)
	trap 'rm -rf "$work"' EXIT
	case $name in
	tn5250)
		tar -xzf "$dir/tn5250.tar.gz" -C "$work"
		cmake -S "$work"/tn5250-* -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		    -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF >/dev/null
		cmake --build "$work/build" --target tn5250 >/dev/null
		install -D -m 755 "$work/build/curses/tn5250" "$prefix/bin/tn5250"
		;;
	drawterm)
		cp -R "$dir/drawterm/." "$work/"
		# X11 graphics, no sound, with the system's C compiler.
		make -C "$work" -s CONF=unix CC=cc X11=/usr AUDIO=none >/dev/null
		install -D -m 755 "$work/drawterm" "$prefix/bin/drawterm"
		;;
	*)
		echo "unknown program $name" >&2
		exit 2
		;;
	esac
	strip "$prefix/bin/$name" 2>/dev/null || true
}

case ${1:-} in
fetch) fetch "$2" ;;
build) build "$2" "$3" "$4" ;;
*)
	echo "usage: third-party.sh fetch DIR | build DIR NAME PREFIX" >&2
	exit 2
	;;
esac
