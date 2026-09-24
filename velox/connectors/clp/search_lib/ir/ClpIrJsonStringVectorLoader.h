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

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "ffi/ir_stream/Deserializer.hpp"
#include "velox/connectors/clp/search_lib/BaseClpCursor.h"
#include "velox/vector/FlatVector.h"
#include "velox/vector/LazyVector.h"

namespace facebook::velox::connector::clp::search_lib {

/// Serializes a decoded IR log event to a compact JSON string, tolerating
/// invalid UTF-8. A decoded IR string field can contain bytes that are not
/// valid UTF-8 (e.g. binary payloads embedded in a log message). nlohmann's
/// default serializer throws `type_error.316` on such input, which previously
/// surfaced as a query failure in whichever operator first forced this lazy
/// load (commonly `TopN::addInput`). This helper serializes with the `replace`
/// error handler so invalid byte sequences become U+FFFD and the query
/// proceeds. nlohmann performs the same UTF-8 validation regardless of handler,
/// so this adds no overhead over the default serialization on valid input.
std::string serializeLogEventJson(const nlohmann::json& logEventJson);

/// VectorLoader that performs on-demand JSON serialization of IR log events.
/// Converts KeyValuePairLogEvent instances into JSON string representations
/// when loading lazy vectors for __json_string columns.
class ClpIrJsonStringVectorLoader : public VectorLoader {
 public:
  ClpIrJsonStringVectorLoader(
      const std::shared_ptr<
          const std::vector<std::unique_ptr<::clp::ffi::KeyValuePairLogEvent>>>&
          filteredLogEvents)
      : filteredLogEvents_(filteredLogEvents) {}

 private:
  void loadInternal(
      RowSet rows,
      ValueHook* hook,
      vector_size_t resultSize,
      VectorPtr* result) override;

  std::shared_ptr<
      const std::vector<std::unique_ptr<::clp::ffi::KeyValuePairLogEvent>>>
      filteredLogEvents_;
};

} // namespace facebook::velox::connector::clp::search_lib
