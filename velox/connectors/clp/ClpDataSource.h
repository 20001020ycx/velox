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

#include <set>

#include "velox/connectors/Connector.h"
#include "velox/connectors/clp/ClpConfig.h"
#include "velox/connectors/clp/search_lib/BaseClpCursor.h"

namespace clp_s {
class BaseColumnReader;
} // namespace clp_s

namespace facebook::velox::connector::clp {

class ClpS3AuthProviderBase;

class ClpDataSource : public DataSource {
 public:
  ClpDataSource(
      const RowTypePtr& outputType,
      const ConnectorTableHandlePtr& tableHandle,
      const connector::ColumnHandleMap& columnHandles,
      velox::memory::MemoryPool* pool,
      std::shared_ptr<const ClpConfig>& clpConfig);

  /**
   * Initializes the cursor for processing a new split. The split must be fully
   * consumed by `next` before another split can be added. This method creates
   * the per-split cursor for reading the split and executes the split's
   * pushdown query.
   * @param split The connector split to process. Must be a `ClpConnectorSplit`.
   */
  void addSplit(std::shared_ptr<ConnectorSplit> split) override;

  /**
   * Fetches the next batch of filtered rows from the current split. Internally
   * loops over the cursor, skipping batches where no rows match the query
   * filter, until either matching rows are found or the split is exhausted.
   * This method only returns matching rows for a single batch; it is up to
   * Velox's TableScan to invoke this method repeatedly to collect all matching
   * rows for a given query plan. A `nullptr` return signals
   * that the split is fully consumed and a new split may be added via
   * `addSplit`.
   * @param size The maximum number of rows to scan per cursor fetch.
   * @param future Unused. CLP data sources perform synchronous I/O and never
   * return `std::nullopt`.
   * @return A `RowVector` containing the filtered rows for the current batch.
   * @return `nullptr` if the split has been fully consumed.
   */
  std::optional<RowVectorPtr> next(uint64_t size, velox::ContinueFuture& future)
      override;

  void addDynamicFilter(
      column_index_t outputChannel,
      const std::shared_ptr<common::Filter>& filter) override {
    VELOX_NYI("Dynamic filters not supported by ClpConnector.");
  }

  uint64_t getCompletedBytes() override {
    return completedBytes_;
  }

  uint64_t getCompletedRows() override {
    return completedRows_;
  }

  std::unordered_map<std::string, RuntimeMetric> getRuntimeStats() override {
    return {};
  }

 private:
  /// Recursively adds fields from the column type to the list of fields to be
  /// retrieved from the data source.
  ///
  /// @param columnType
  /// @param parentName The name of the parent field (used for nested fields).
  void addFieldsRecursively(
      const TypePtr& columnType,
      const std::string& parentName);

  ClpConfig::StorageType storageType_;
  velox::memory::MemoryPool* pool_;
  RowTypePtr outputType_;
  std::set<std::string> columnUntypedNames_;
  uint64_t completedRows_{0};
  uint64_t completedBytes_{0};

  std::vector<search_lib::Field> fields_;

  std::unique_ptr<search_lib::BaseClpCursor> cursor_;
  std::shared_ptr<ClpS3AuthProviderBase> s3AuthProvider_;
};

} // namespace facebook::velox::connector::clp
