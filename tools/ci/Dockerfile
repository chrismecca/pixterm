# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# The toolchain for every check that needs no hardware: the same
# Debian release the image is built from, with LLVM's compiler,
# standard library, linker, sanitizers and tools.
FROM debian:trixie-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      clang \
      clang-format \
      clang-tidy \
      cmake \
      git \
      libc++-dev \
      libc++abi-dev \
      libclang-rt-dev \
      libmotif-dev \
      libssl-dev \
      libx11-dev \
      libxrandr-dev \
      libxt-dev \
      lld \
      ninja-build \
      pkg-config \
      reuse \
 && rm -rf /var/lib/apt/lists/*

# The source tree belongs to another user; git must still read it.
RUN git config --system --add safe.directory '*'
