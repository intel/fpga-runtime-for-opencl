# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM ubuntu:20.04

# Optionally override uid of default user in container, e.g.,
# docker build --build-arg uid=1001 ...
ARG uid

RUN \
  # Klocwork uses 32-bit binaries on x64_64
  dpkg --add-architecture i386 \ 
  && apt-get -y update \
  && DEBIAN_FRONTEND=noninteractive apt-get -y dist-upgrade \
  build-essential \
  ca-certificates \
  cmake \
  curl \
  git \
  libarchive-tools \
  libelf-dev \
  libtinfo5 \
  libxml2 \
  ninja-build \
  sudo \
  zlib1g-dev \
  # faster compression for actions/cache
  zstd \
  # Klocwork specific dependencies below
  libc6:i386 \
  libgcc1:i386 \
  lib32tinfo6 \
  libncurses5:i386 \
  libncursesw5:i386 \
  libaio1 \
  numactl \
  lsb-core \
  && apt-get -y clean \
  && useradd --system ${uid:+--uid "$uid"} --user-group --shell /sbin/nologin --create-home --home-dir /home/klocwork klocwork \
  && echo 'klocwork ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/klocwork

USER klocwork
WORKDIR /home/klocwork

ARG klocwork_install_dir=/opt/klocwork
ARG klocwork_build_tools_url
ARG klocwork_desktop_tools_url
ARG klocwork_license_server

RUN \
  sudo install -d -o klocwork -g klocwork "$klocwork_install_dir" \
  && curl -L -o klocwork "$klocwork_build_tools_url" \
  && bsdtar -xf klocwork --strip-components=1 -C "$klocwork_install_dir" \
  && rm klocwork \
  && sudo chown -R root:root "$klocwork_install_dir" \
  && sudo chmod -R u=rwX,g=rX,o=rX "$klocwork_install_dir"

ENV PATH="$klocwork_install_dir/bin:$PATH"
RUN kwbuildproject --version
