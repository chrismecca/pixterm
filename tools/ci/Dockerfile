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
      libxext-dev \
      libxrandr-dev \
      libxt-dev \
      lld \
      ninja-build \
      pkg-config \
      reuse \
      shellcheck \
 && rm -rf /var/lib/apt/lists/*

# actionlint checks the GitHub workflows. Debian does not package it.
ARG ACTIONLINT=1.7.12
RUN arch=$(dpkg --print-architecture) \
 && case $arch in \
      amd64) sum=8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8 ;; \
      arm64) sum=325e971b6ba9bfa504672e29be93c24981eeb1c07576d730e9f7c8805afff0c6 ;; \
    esac \
 && f=actionlint_${ACTIONLINT}_linux_$arch.tar.gz \
 && apt-get update && apt-get install -y --no-install-recommends curl \
 && curl -fsSLo /tmp/$f https://github.com/rhysd/actionlint/releases/download/v$ACTIONLINT/$f \
 && echo "$sum  /tmp/$f" | sha256sum -c - \
 && tar -xzf /tmp/$f -C /usr/local/bin actionlint \
 && rm /tmp/$f \
 && apt-get purge -y curl && apt-get autoremove -y && rm -rf /var/lib/apt/lists/*

# The source tree belongs to another user; git must still read it.
RUN git config --system --add safe.directory '*'
