# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Builder image with dependencies and pre-populated ccache for faster CI builds
FROM ghcr.io/myoung34/docker-github-actions-runner:ubuntu-jammy

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Set timezone to avoid interactive prompts during apt installations
ENV TZ=Etc/UTC
ENV DEBIAN_FRONTEND=noninteractive

# Copy dependency installation scripts and CMake modules together so that setup-common.sh's
# relative path resolution (SCRIPT_DIR/../CMake/...) works correctly (after rarely-changing
# layers for better caching)
COPY scripts /tmp/velox-deps/scripts/
COPY CMake/resolve_dependency_modules /tmp/velox-deps/CMake/resolve_dependency_modules/

ENV UV_TOOL_BIN_DIR=/usr/local/bin
ENV UV_INSTALL_DIR=/usr/local/bin

RUN /tmp/velox-deps/scripts/setup-ubuntu.sh \
    && rm -rf /tmp/velox-deps

ENV CCACHE_DIR=/var/cache/ccache

# Disable compression to trade disk space for speed in CI builds
ENV CCACHE_COMPRESSLEVEL=0
ENV CCACHE_MAXSIZE=5G

# Ignore working directory in cache keys so warmup cache (built in /tmp/velox-src/) is reused
# by CI builds (which run in /__w/velox/velox/)
ENV CCACHE_NOHASHDIR=true

# Build velox once to warm up ccache
# NOTE:
# - We set `CCACHE_BASEDIR` so cache keys use relative paths.
# - We clear the stats after warmup so that CI builds only show their own cache hits.
COPY . /tmp/velox-src/
WORKDIR /tmp/velox-src
RUN CCACHE_BASEDIR=/tmp/velox-src make release TREAT_WARNINGS_AS_ERRORS=0 \
    && echo "CCache statistics after warmup build:" \
    && ccache --verbose --show-stats \
    && ccache --zero-stats
RUN rm -rf /tmp/velox-src

WORKDIR /
