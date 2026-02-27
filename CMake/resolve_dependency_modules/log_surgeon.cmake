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

FetchContent_Declare(
  log_surgeon
  GIT_REPOSITORY https://github.com/y-scope/log-surgeon.git
  GIT_TAG 193e1f91eb137bb935a7f44b13cc8dd945a8d742
  OVERRIDE_FIND_PACKAGE
)

set(log_surgeon_BUILD_TESTING OFF)
FetchContent_MakeAvailable(log_surgeon)

# To work around y-scope/log-surgeon#155
install(TARGETS GSL EXPORT log_surgeon-targets)
