# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM centos:8

WORKDIR /work

# Before using a new script, update .github/workflows/container.yml
# to extend the `paths` on which the workflow runs.
COPY scripts/. ./

RUN sed -i '/^enabled=/s#0#1#' /etc/yum.repos.d/CentOS-Linux-PowerTools.repo \
  && grep '^enabled=1$' /etc/yum.repos.d/CentOS-Linux-PowerTools.repo \
  && yum -y upgrade \
  && yum -y install \
  cmake \
  curl \
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
  make \
  ncurses-compat-libs-6.1 \
  ninja-build \
  perl \
  python3 \
  sudo \
  which \
  zlib-devel \
  && yum -y clean all \
  && ./install_aocl.sh /opt/aocl \
  && useradd --system --user-group --shell /sbin/nologin --create-home --home-dir /home/build build \
  && echo 'build ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/build \
  && rm -rf "$PWD"

USER build
WORKDIR /home/build

ENV PATH="/opt/aocl/hld/bin:$PATH"
RUN aoc -version
