# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM rockylinux:9

# Optionally override uid of default user in container, e.g.,
# docker build --build-arg uid=1001 ...
ARG uid

WORKDIR /work

# Before using a new script, update .github/workflows/container.yml
# to extend the `paths` on which the workflow runs.
COPY scripts/. ./

RUN \
  sed -i '/^\[crb\]$/,/^\[/s#^enabled=0$#enabled=1#' /etc/yum.repos.d/rocky.repo \
  && grep '^enabled=1$' /etc/yum.repos.d/rocky.repo \
  && sed -i '/^\[devel\]$/,/^\[/s#^enabled=0$#enabled=1#' /etc/yum.repos.d/rocky-devel.repo \
  && grep '^enabled=1$' /etc/yum.repos.d/rocky-devel.repo \
  && yum -y upgrade \
  && yum -y install \
  cmake \
  # curl \ Start from rockylinux 9.2, curl is available by default
  elfutils-libelf-devel \
  gcc \
  gcc-c++ \
  git \
  jq \
  libasan \
  liblsan \
  libnsl \
  libtsan \
  libubsan \
  libxcrypt-compat \
  make \
  ncurses-compat-libs \
  ninja-build \
  perl \
  python3 \
  sudo \
  which \
  zlib-devel \
  && yum -y clean all \
  && ./install_aocl.sh /opt/aocl \
  && useradd --system ${uid:+--uid "$uid"} --user-group --shell /sbin/nologin --create-home --home-dir /home/build build \
  && echo 'build ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/build \
  && rm -rf "$PWD"

USER build
WORKDIR /home/build

ENV PATH="/opt/aocl/hld/bin:$PATH"
RUN aoc -version
