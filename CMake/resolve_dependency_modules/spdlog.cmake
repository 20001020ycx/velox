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

set(VELOX_SPDLOG_BUILD_VERSION 1.15.3)
set(
  VELOX_SPDLOG_BUILD_SHA256_CHECKSUM
  15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67
)
set(
  VELOX_SPDLOG_SOURCE_URL
  "https://github.com/gabime/spdlog/archive/refs/tags/v${VELOX_SPDLOG_BUILD_VERSION}.tar.gz"
)

velox_resolve_dependency_url(SPDLOG)

message(STATUS "Building spdlog from source")

FetchContent_Declare(
  spdlog
  URL ${VELOX_SPDLOG_SOURCE_URL}
  URL_HASH ${VELOX_SPDLOG_BUILD_SHA256_CHECKSUM}
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
  SYSTEM
)

set(SPDLOG_FMT_EXTERNAL ON)
FetchContent_MakeAvailable(spdlog)
