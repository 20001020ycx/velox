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

set(VELOX_YSTDLIB_BUILD_VERSION 9ed78cd)
set(
  VELOX_YSTDLIB_BUILD_SHA256_CHECKSUM
  65990dc2bcc4a355c2181bfe31a7800f492309d1bcd340f52a34e85047e61bc8
)
set(
  VELOX_YSTDLIB_SOURCE_URL
  "https://github.com/y-scope/ystdlib-cpp/archive/${VELOX_YSTDLIB_BUILD_VERSION}.tar.gz"
)

velox_resolve_dependency_url(YSTDLIB)

message(STATUS "Building ystdlib from source")

FetchContent_Declare(
  ystdlib
  URL ${VELOX_YSTDLIB_SOURCE_URL}
  URL_HASH ${VELOX_YSTDLIB_BUILD_SHA256_CHECKSUM}
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
  SYSTEM
)

set(ystdlib_BUILD_TESTING OFF)

FetchContent_MakeAvailable(ystdlib)
