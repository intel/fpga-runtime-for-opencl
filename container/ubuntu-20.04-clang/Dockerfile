# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

FROM ubuntu:20.04

ARG clang_installdir=/opt/clang
ARG clang_version=13.0.0
ARG clang_archive=clang+llvm-$clang_version-x86_64-linux-gnu-ubuntu-20.04.tar.xz
ARG clang_archive_sig=$clang_archive.sig
ARG clang_archive_url=https://github.com/llvm/llvm-project/releases/download/llvmorg-$clang_version/$clang_archive
ARG clang_archive_sig_url=$clang_archive_url.sig

# Optionally override uid of default user in container, e.g.,
# docker build --build-arg uid=1001 ...
ARG uid

WORKDIR /work

# https://releases.llvm.org/download.html
# https://github.com/llvm/llvm-project/releases/download/llvmorg-9.0.1/tstellar-gpg-key.asc
COPY container/ubuntu-20.04-clang/tstellar.gpg ./

RUN apt-get -y update \
  && DEBIAN_FRONTEND=noninteractive apt-get -y dist-upgrade \
  ca-certificates \
  curl \
  git \
  sudo \
  xz-utils \
  && apt-get -y clean \
  && curl -L -o "$clang_archive" "$clang_archive_url" \
  && curl -L -o "$clang_archive_sig" "$clang_archive_sig_url" \
  && gpgv --keyring "$PWD/tstellar.gpg" "$clang_archive_sig" "$clang_archive" \
  && mkdir "$clang_installdir" \
  && tar --strip-components=1 -C "$clang_installdir" -xf "$clang_archive" \
  && chown -R root:root "$clang_installdir" \
  && chmod -R u=rwX,g=rX,o=rX "$clang_installdir" \
  && useradd --system ${uid:+--uid "$uid"} --user-group --shell /sbin/nologin --create-home --home-dir /home/build build \
  && echo 'build ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/build \
  && rm -rf "$PWD"

USER build
WORKDIR /home/build

ENV PATH="$clang_installdir/bin:$PATH"
RUN clang --version
