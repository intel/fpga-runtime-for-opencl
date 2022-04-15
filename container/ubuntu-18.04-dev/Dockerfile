# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM ubuntu:18.04

# Optionally override uid of default user in container, e.g.,
# docker build --build-arg uid=1001 ...
ARG uid

WORKDIR /work

# Before using a new script, update .github/workflows/container.yml
# to extend the `paths` on which the workflow runs.
COPY scripts/. ./

# Ubuntu 18.04 ships with Git 2.17.1 [1] while actions/checkout@v2
# requires Git 2.18 or newer [2, 3]. Install the latest stable release
# from the PPA [4] maintained by Anders Kaseorg <andersk@mit.edu> [5],
# who has previously co-maintained the official Debian git package [6].
#
# [1] https://packages.ubuntu.com/bionic/git
# [2] https://github.com/rocker-org/rocker-versioned2/issues/52
# [3] https://github.com/actions/checkout/issues/238
# [4] https://launchpad.net/~git-core/+archive/ubuntu/ppa
# [5] https://launchpad.net/~andersk
# [6] https://tracker.debian.org/pkg/git
RUN apt-get -y update \
  && apt-get -y install software-properties-common \
  && add-apt-repository ppa:git-core/ppa \
  && DEBIAN_FRONTEND=noninteractive apt-get -y dist-upgrade \
  build-essential \
  ca-certificates \
  cmake \
  curl \
  git \
  jq \
  libelf-dev \
  libtinfo5 \
  libxml2 \
  ninja-build \
  python3 \
  sudo \
  zlib1g-dev \
  && apt-get -y clean \
  && ./install_aocl.sh /opt/aocl \
  && useradd --system ${uid:+--uid "$uid"} --user-group --shell /sbin/nologin --create-home --home-dir /home/build build \
  && echo 'build ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/build \
  && rm -rf "$PWD"

USER build
WORKDIR /home/build

ENV PATH="/opt/aocl/hld/bin:$PATH"
RUN aoc -version
