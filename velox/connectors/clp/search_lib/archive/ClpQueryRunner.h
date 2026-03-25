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

#include "clp_s/search/QueryRunner.hpp"

namespace clp_s {
class SchemaReader;
class SchemaTree;
class ArchiveReader;
class BaseColumnReader;
} // namespace clp_s

namespace clp_s::search {
class SchemaMatch;
class Projection;
} // namespace clp_s::search

namespace clp_s::search::ast {
class Expression;
} // namespace clp_s::search::ast

namespace facebook::velox::connector::clp::search_lib {

/// Extends the generic QueryRunner to support column projection and row
/// filtering over CLP-S archives. It is used by the Velox-CLP connector to
/// efficiently identify matching rows and project relevant columns, which are
/// then consumed by the ClpVectorLoader.
class ClpQueryRunner : public clp_s::search::QueryRunner {
 public:
  ClpQueryRunner(
      const std::shared_ptr<clp_s::search::SchemaMatch>& match,
      const std::shared_ptr<clp_s::search::ast::Expression>& expr,
      const std::shared_ptr<clp_s::ArchiveReader>& archiveReader,
      bool ignoreCase,
      const std::shared_ptr<clp_s::search::Projection>& projection)
      : clp_s::search::QueryRunner(match, expr, archiveReader, ignoreCase),
        archiveReader_(archiveReader),
        projection_(projection) {}

  /// Initializes the filter with schema information and column readers.
  ///
  /// @param schemaReader A pointer to the SchemaReader.
  /// @param columnMap An unordered map associating column IDs with
  /// BaseColumnReader pointers.
  void init(
      clp_s::SchemaReader* schemaReader,
      std::unordered_map<int32_t, clp_s::BaseColumnReader*> const& columnMap)
      override;

  /// Fetches the next set of rows from the cursor.
  ///
  /// @param numRows The maximum number of rows to fetch.
  /// @param filteredRowIndices A vector to store the row indices that match the
  /// filter.
  /// @return The number of rows scanned.
  uint64_t fetchNext(
      uint64_t numRows,
      const std::shared_ptr<std::vector<uint64_t>>& filteredRowIndices);

  /// @return A reference to the vector of BaseColumnReader pointers that
  /// represent the columns involved in the scanning operation.
  const std::vector<clp_s::BaseColumnReader*>& getProjectedColumns() {
    return projectedColumns_;
  }

  /// Gets the global log event index for a given local message index in the
  /// current schema table.
  /// @param messageIndex The local message index within the current schema table.
  /// @return The global log event index, or 0 if the reader is unavailable.
  int64_t getLogEventId(uint64_t messageIndex) const;

 private:
  std::shared_ptr<clp_s::search::ast::Expression> expr_;
  std::shared_ptr<clp_s::ArchiveReader> archiveReader_;
  std::shared_ptr<clp_s::search::Projection> projection_;
  std::vector<clp_s::BaseColumnReader*> projectedColumns_;

  uint64_t curMessage_{};
  uint64_t numMessages_{};
  /// Column reader for CLP-S's internal log_event_idx metadata field, which
  /// maps each row's local index within a schema table to its global log event
  /// index across the entire archive.
  ///
  /// Re-initialized per schema table: init() looks up the column ID in the schema
  /// tree and finds the matching reader in the column map for the current table.
  /// nullptr if unavailable (e.g., archive created without --record-log-order).
  clp_s::BaseColumnReader* logEventIdColumnReader_{nullptr};
};

} // namespace facebook::velox::connector::clp::search_lib
