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
include_guard(GLOBAL)

set(VELOX_XXHASH_VERSION 0.8.3)
set(
  VELOX_XXHASH_BUILD_SHA256_CHECKSUM
  aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
)
set(
  VELOX_XXHASH_SOURCE_URL
  "https://github.com/Cyan4973/xxHash/archive/refs/tags/v${VELOX_XXHASH_VERSION}.tar.gz"
)

velox_resolve_dependency_url(XXHASH)

message(STATUS "Building xxHash from source")

FetchContent_Declare(
  xxHash
  URL ${VELOX_XXHASH_SOURCE_URL}
  URL_HASH ${VELOX_XXHASH_BUILD_SHA256_CHECKSUM}
  SOURCE_SUBDIR cmake_unofficial
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
  SYSTEM
)

set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "Build the xxhsum binary" FORCE)

FetchContent_MakeAvailable(xxHash)
