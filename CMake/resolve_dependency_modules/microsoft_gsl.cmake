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

# Version you want to build
set(VELOX_GSL_BUILD_VERSION 4.0.0)
set(
  VELOX_GSL_BUILD_SHA256_CHECKSUM
  f0e32cb10654fea91ad56bde89170d78cfbf4363ee0b01d8f097de2ba49f6ce9
)
set(
  VELOX_GSL_SOURCE_URL
  "https://github.com/microsoft/GSL/archive/refs/tags/v${VELOX_GSL_BUILD_VERSION}.tar.gz"
)

velox_resolve_dependency_url(GSL)

message(STATUS "Building Microsoft.GSL from source")

FetchContent_Declare(
  Microsoft.GSL
  URL ${VELOX_GSL_SOURCE_URL}
  URL_HASH ${VELOX_GSL_BUILD_SHA256_CHECKSUM}
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
  SYSTEM
)

FetchContent_MakeAvailable(Microsoft.GSL)
