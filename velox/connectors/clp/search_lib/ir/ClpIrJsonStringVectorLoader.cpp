/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/connectors/clp/search_lib/ir/ClpIrJsonStringVectorLoader.h"

#include "velox/connectors/clp/search_lib/BaseClpCursor.h"
#include "velox/connectors/clp/search_lib/ClpTimestampsUtils.h"

namespace facebook::velox::connector::clp::search_lib {

std::string serializeLogEventJson(const nlohmann::json& logEventJson) {
  // Serialize with the `replace` error handler (see the header for rationale).
  // nlohmann runs the same UTF-8 validation pass regardless of handler, so this
  // costs the same as the default `dump()` on valid input and never throws;
  // there is no per-row overhead on the common valid-UTF-8 path.
  return logEventJson.dump(
      -1, ' ', false, nlohmann::json::error_handler_t::replace);
}

void ClpIrJsonStringVectorLoader::loadInternal(
    RowSet rows,
    ValueHook* hook,
    vector_size_t resultSize,
    VectorPtr* result) {
  VELOX_CHECK_NOT_NULL(result, "result vector must not be null");
  VELOX_CHECK_NULL(
      hook, "ClpIrJsonStringVectorLoader doesn't support ValueHook");

  auto vector = *result;
  auto* stringVector = vector->asFlatVector<StringView>();
  for (vector_size_t const vectorIndex : rows) {
    const auto& logEvent = filteredLogEvents_->at(vectorIndex);
    auto serializedResult = logEvent->serialize_to_json();
    if (serializedResult.has_error()) {
      auto error = serializedResult.error();
      VELOX_FAIL(
          "Cannot serialize IR to JSON. {}: {}",
          error.category().name(),
          error.message());
    }

    std::string const jsonString =
        serializeLogEventJson(serializedResult.value().second);
    stringVector->set(vectorIndex, StringView(jsonString));
    stringVector->setNull(vectorIndex, false);
  }
}

} // namespace facebook::velox::connector::clp::search_lib
