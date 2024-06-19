# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM opensuse/leap:15

# Optionally override uid of default user in container, e.g.,
# docker build --build-arg uid=1001 ...
ARG uid

WORKDIR /work

# Before using a new script, update .github/workflows/container.yml
# to extend the `paths` on which the workflow runs.
COPY scripts/. ./

RUN zypper -n update \
  && zypper -n install \
  ccache \
  cmake \
  curl \
  gcc \
  gcc-c++ \
  git \
  gzip \
  jq \
  libelf-devel \
  libncurses5 \
  make \
  ninja \
  perl \
  python3 \
  sudo \
  tar \
  which \
  zlib-devel \
  && zypper -n clean \
  && ./install_aocl.sh /opt/aocl \
  && useradd --system ${uid:+--uid "$uid"} --user-group --shell /sbin/nologin --create-home --home-dir /home/build build \
  && echo 'build ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/build \
  && rm -rf "$PWD"

USER build
WORKDIR /home/build

ENV PATH="/opt/aocl/hld/bin:$PATH"
# Workaround for libnsl.so.1 not installed by default on the latest OpenSUSE image
RUN mkdir -p /tmp/aoc && ln -snf /usr/lib64/libnsl.so.2.0.0 /tmp/aoc/libnsl.so.1
ENV LD_LIBRARY_PATH="/tmp/aoc:$LD_LIBRARY_PATH"
RUN aoc -version
