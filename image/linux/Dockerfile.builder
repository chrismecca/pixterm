# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT
#
# Native Debian container used to customize a Raspberry Pi OS image.
# Foreign binaries inside the chroot run through the host's qemu binfmt
# registration (see build.sh); arm64 runs natively on Apple Silicon.
FROM debian:trixie-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      xz-utils fdisk e2fsprogs dosfstools mount util-linux ca-certificates apt-utils \
 && rm -rf /var/lib/apt/lists/*
