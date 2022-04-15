# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM ubuntu:20.04

# Optionally override uid of default user in container, e.g.,
# docker build --build-arg uid=1001 ...
ARG uid

WORKDIR /work

# Before using a new script, update .github/workflows/container.yml
# to extend the `paths` on which the workflow runs.
COPY scripts/. ./

RUN apt-get -y update \
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
