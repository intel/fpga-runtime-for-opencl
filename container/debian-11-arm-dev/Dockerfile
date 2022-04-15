# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM debian:11

# Optionally override uid of default user in container, e.g.,
# docker build --build-arg uid=1001 ...
ARG uid

WORKDIR /work

# Before using a new script, update .github/workflows/container.yml
# to extend the `paths` on which the workflow runs.
COPY scripts/. ./

# https://wiki.debian.org/Multiarch/HOWTO
RUN dpkg --add-architecture armhf \
  && apt-get -y update \
  && DEBIAN_FRONTEND=noninteractive apt-get -y dist-upgrade \
  build-essential \
  ca-certificates \
  cmake \
  curl \
  git \
  jq \
  libtinfo5 \
  libxml2 \
  ninja-build \
  python3 \
  sudo \
  # Install cross-compile toolchain and libraries
  g++-arm-linux-gnueabihf \
  gcc-arm-linux-gnueabihf \
  libelf-dev:armhf \
  zlib1g-dev:armhf \
  && apt-get -y clean \
  && useradd --system ${uid:+--uid "$uid"} --user-group --shell /sbin/nologin --create-home --home-dir /home/build build \
  && echo 'build ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/build \
  && rm -rf "$PWD"

USER build
WORKDIR /home/build
