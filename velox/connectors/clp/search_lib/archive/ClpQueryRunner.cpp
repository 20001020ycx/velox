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

#include <glog/logging.h>

#include "clp_s/ColumnReader.hpp"
#include "velox/connectors/clp/search_lib/archive/ClpQueryRunner.h"

using namespace clp_s;
using namespace clp_s::search;

namespace facebook::velox::connector::clp::search_lib {

void ClpQueryRunner::init(
    clp_s::SchemaReader* schemaReader,
    std::unordered_map<int32_t, clp_s::BaseColumnReader*> const& columnMap) {
  numMessages_ = schemaReader->get_num_messages();
  curMessage_ = 0;
  clear_readers();

  projectedColumns_.clear();
  auto matchingNodesList = projection_->get_ordered_matching_nodes();
  for (const auto& nodeIds : matchingNodesList) {
    if (nodeIds.empty()) {
      projectedColumns_.push_back(nullptr);
      continue;
    }

    // Try to find a matching column in columnMap
    bool foundReader = false;
    for (const auto nodeId : nodeIds) {
      auto columnIt = columnMap.find(nodeId);
      if (columnIt != columnMap.end()) {
        projectedColumns_.push_back(columnIt->second);
        foundReader = true;
        break;
      }
    }

    if (!foundReader) {
      projectedColumns_.push_back(nullptr);
    }
  }

  // Look up the log_event_idx column reader from the archive schema tree.
  logEventIdColumnReader_ = nullptr;
  auto logEventIdColumnId = archiveReader_->get_schema_tree()
      ->get_metadata_field_id(clp_s::constants::cLogEventIdxName);
  if (logEventIdColumnId >= 0) {
    auto it = columnMap.find(logEventIdColumnId);
    if (it != columnMap.end()) {
      logEventIdColumnReader_ = it->second;
    } else {
      VLOG(2) << "ClpQueryRunner::init: log_event_idx columnId=" << logEventIdColumnId
              << " not found in columnMap (size=" << columnMap.size() << ")";
    }
  } else {
    VLOG(2) << "ClpQueryRunner::init: log_event_idx not found in schema tree";
  }

  for (const auto& [columnId, columnReader] : columnMap) {
    initialize_reader(columnId, columnReader);
  }
}

uint64_t ClpQueryRunner::fetchNext(
    uint64_t numRows,
    const std::shared_ptr<std::vector<uint64_t>>& filteredRowIndices) {
  size_t rowsfiltered{0};
  size_t rowsScanned{0};
  while (curMessage_ < numMessages_) {
    if (filter(curMessage_)) {
      filteredRowIndices->emplace_back(curMessage_);
      rowsfiltered += 1;
    }

    curMessage_ += 1;
    rowsScanned += 1;
    if (rowsfiltered >= numRows) {
      break;
    }
  }
  return rowsScanned;
}

int64_t ClpQueryRunner::getLogEventId(uint64_t messageIndex) const {
  if (logEventIdColumnReader_) {
    return std::get<int64_t>(logEventIdColumnReader_->extract_value(messageIndex));
  }
  return 0;
}

} // namespace facebook::velox::connector::clp::search_lib
