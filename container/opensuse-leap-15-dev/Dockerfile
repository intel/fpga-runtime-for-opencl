# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM opensuse/leap:15

WORKDIR /work

# Before using a new script, update .github/workflows/container.yml
# to extend the `paths` on which the workflow runs.
COPY scripts/. ./

RUN zypper -n update \
  && zypper -n install \
  cmake \
  curl \
  gcc \
  gcc-c++ \
  git \
  gzip \
  libelf-devel \
  libncurses5 \
  make \
  ninja \
  perl \
  sudo \
  tar \
  which \
  zlib-devel \
  && zypper -n clean \
  && ./install_aocl.sh /opt/aocl \
  && useradd --system --user-group --shell /sbin/nologin --create-home --home-dir /home/build build \
  && echo 'build ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/build \
  && rm -rf "$PWD"

USER build
WORKDIR /home/build

ENV PATH="/opt/aocl/hld/bin:$PATH"
RUN aoc -version
