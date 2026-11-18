# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# The session test, with no hardware: Xvfb stands in for Xorg, sshd on
# localhost for a remote host, netcat for a Telnet host,
# tests/tools/fake_rexecd for rexec and rsh hosts, Xvnc for a VNC host,
# FreeRDP's shadow server for an RDP host, xdm for an XDMCP host and a
# socat pseudo-terminal for a serial console. Built on the toolchain
# image (tools/ci/Dockerfile).
FROM pixterm-ci

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      freerdp3-shadow-x11 \
      freerdp3-x11 \
      imagemagick \
      inetutils-telnet \
      libcap2-bin \
      mwm \
      netcat-openbsd \
      openssh-client \
      openssh-server \
      picocom \
      procps \
      socat \
      tigervnc-standalone-server \
      tigervnc-viewer \
      winpr3-utils \
      x11-apps \
      x11-utils \
      x11-xkb-utils \
      x11-xserver-utils \
      x3270 \
      xauth \
      xdm \
      xdotool \
      xfonts-100dpi \
      xfonts-75dpi \
      xfonts-base \
      xfonts-x3270-misc \
      xterm \
      xvfb \
 && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash alice && echo 'alice:secret' | chpasswd \
 && printf '#!/bin/sh\nexec xterm\n' > /home/alice/.xsession && chmod 755 /home/alice/.xsession \
 && useradd -r -s /usr/sbin/nologin pixterm \
 && mkdir -p /run/sshd && ssh-keygen -A
