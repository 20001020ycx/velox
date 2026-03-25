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
#include <nlohmann/json.hpp>

#include "clp_s/ArchiveReader.hpp"
#include "clp_s/archive_constants.hpp"
#include "clp_s/SingleFileArchiveDefs.hpp"
#include "clp_s/search/EvaluateRangeIndexFilters.hpp"
#include "clp_s/search/EvaluateTimestampIndex.hpp"
#include "clp_s/search/ast/EmptyExpr.hpp"
#include "clp_s/search/ast/SearchUtils.hpp"
#include "clp_s/search/ast/SetTimestampLiteralPrecision.hpp"
#include "clp_s/search/ast/TimestampLiteral.hpp"
#include "velox/connectors/clp/ClpColumnHandle.h"
#include "velox/connectors/clp/search_lib/archive/ClpArchiveCursor.h"
#include "velox/connectors/clp/search_lib/archive/ClpArchiveJsonStringVectorLoader.h"
#include "velox/connectors/clp/search_lib/archive/ClpArchiveVectorLoader.h"
#include "velox/connectors/clp/search_lib/archive/ClpQueryRunner.h"

using namespace clp_s;
using namespace clp_s::search;
using namespace clp_s::search::ast;

namespace facebook::velox::connector::clp::search_lib {

ClpArchiveCursor::ClpArchiveCursor(
    clp_s::InputSource inputSource,
    std::string_view splitPath)
    : BaseClpCursor(inputSource, splitPath),
      archiveReader_(std::make_shared<ArchiveReader>()),
      filteredRowIndices_(std::make_shared<std::vector<uint64_t>>()) {}

ClpArchiveCursor::~ClpArchiveCursor() {
  if (currentSplitLoaded_) {
    archiveReader_->close();
  }
}

uint64_t ClpArchiveCursor::fetchNext(uint64_t numRows) {
  filteredRowIndices_->clear();
  projectedColumnIndex_ = 0;
  columnIndex_ = 0;

  if (ErrorCode::Success != errorCode_) {
    return 0;
  }

  if (false == currentSplitLoaded_) {
    errorCode_ = loadSplit();
    if (ErrorCode::Success != errorCode_) {
      return 0;
    }
  }

  while (currentSchemaIndex_ < matchedSchemas_.size()) {
    if (false == currentSchemaTableLoaded_) {
      currentSchemaId_ = matchedSchemas_[currentSchemaIndex_];
      if (EvaluatedValue::False ==
          queryRunner_->schema_init(currentSchemaId_)) {
        currentSchemaIndex_ += 1;
        currentSchemaTableLoaded_ = false;
        errorCode_ = ErrorCode::DictionaryNotFound;
        continue;
      }

      schemaReader_ =
          &archiveReader_->read_schema_table(currentSchemaId_, false, false);
      schemaReader_->initialize_filter_with_column_map(*queryRunner_);
      VLOG(2) << "fetchNext: loading schema table schemaId=" << currentSchemaId_
              << " schemaIndex=" << currentSchemaIndex_;

      errorCode_ = ErrorCode::Success;
      currentSchemaTableLoaded_ = true;
    }

    auto rowsScanned = queryRunner_->fetchNext(numRows, filteredRowIndices_);
    if (false == filteredRowIndices_->empty()) {
      return rowsScanned;
    }

    currentSchemaIndex_ += 1;
    currentSchemaTableLoaded_ = false;
  }

  return 0;
}

size_t ClpArchiveCursor::getNumFilteredRows() const {
  return filteredRowIndices_->size();
}

VectorPtr ClpArchiveCursor::createVector(
    memory::MemoryPool* pool,
    const TypePtr& vectorType,
    size_t vectorSize) {
  auto projectedColumns = getProjectedColumns();
  VELOX_CHECK_EQ(
      projectedColumns.size() + jsonStringColumnIndices_.size() +
          metadataColumnIndices_.size(),
      outputColumns_.size(),
      "Column classification mismatch for output type '{}': "
      "projected ({}) + JSON string ({}) + metadata ({}) != output columns ({})",
      vectorType->toString(),
      projectedColumns.size(),
      jsonStringColumnIndices_.size(),
      metadataColumnIndices_.size(),
      outputColumns_.size());
  return createVectorHelper(pool, vectorType, vectorSize, projectedColumns);
}

const std::vector<clp_s::BaseColumnReader*>&
ClpArchiveCursor::getProjectedColumns() const {
  if (queryRunner_) {
    return queryRunner_->getProjectedColumns();
  }
  static std::vector<clp_s::BaseColumnReader*> const kEmpty;
  return kEmpty;
}

ErrorCode ClpArchiveCursor::loadSplit() {
  auto networkAuthOption = inputSource_ == InputSource::Filesystem
      ? NetworkAuthOption{.method = AuthMethod::None}
      : NetworkAuthOption{.method = AuthMethod::S3PresignedUrlV4};

  try {
    archiveReader_->open(
        get_path_object_for_raw_path(splitPath_), networkAuthOption);
  } catch (std::exception& e) {
    VLOG(2) << "Failed to open archive file: " << splitPath_ << ": " << e.what();
    return ErrorCode::InternalError;
  }

  auto timestampDict = archiveReader_->get_timestamp_dictionary();
  auto schemaTree = archiveReader_->get_schema_tree();
  auto schemaMap = archiveReader_->get_schema_map();

  auto const defaultTimestampPrecision{
      archiveReader_->has_deprecated_timestamp_format()
          ? TimestampLiteral::Precision::Milliseconds
          : TimestampLiteral::Precision::Nanoseconds};
  SetTimestampLiteralPrecision timestampPrecisionPass{
      defaultTimestampPrecision};
  expr_ = timestampPrecisionPass.run(expr_);

  EvaluateTimestampIndex timestampIndex(timestampDict);
  if (clp_s::EvaluatedValue::False == timestampIndex.run(expr_)) {
    VLOG(2) << "No matching timestamp ranges for query '" << query_ << "'";
    return ErrorCode::InvalidTimestampRange;
  }

  auto const& rangeIndex = archiveReader_->get_range_index();
  EvaluateRangeIndexFilters rangeIndexFilter{rangeIndex, true};
  if (expr_ = rangeIndexFilter.run(expr_);
      std::dynamic_pointer_cast<EmptyExpr>(expr_)) {
    VLOG(2) << "No matching range index entries for query '" << query_ << "'";
    return ErrorCode::SchemaNotFound;
  }

  schemaMatch_ = std::make_shared<SchemaMatch>(schemaTree, schemaMap);
  if (expr_ = schemaMatch_->run(expr_);
      std::dynamic_pointer_cast<EmptyExpr>(expr_)) {
    VLOG(2) << "No matching schemas for query '" << query_ << "'";
    return ErrorCode::SchemaNotFound;
  }

  projection_ = std::make_shared<Projection>(
      outputColumns_.empty() ? ReturnAllColumns : ReturnSelectedColumns, true);
  try {
    for (size_t i = 0; i < outputColumns_.size(); i++) {
      auto const& column = outputColumns_[i];
      if (ClpColumnHandle::jsonStringColumnName_ == column.name) {
        jsonStringColumnIndices_.insert(i);
        continue;
      }
      if (isMetadataColumn(column.name)) {
        metadataColumnIndices_.insert(i);
        continue;
      }
      std::vector<std::string> descriptorTokens;
      std::string descriptorNamespace;
      if (false ==
          tokenize_column_descriptor(
              column.name, descriptorTokens, descriptorNamespace)) {
        VLOG(2) << "Can not tokenize invalid column: '" << column.name << "'";
        return ErrorCode::InternalError;
      }

      auto columnDescriptor = ColumnDescriptor::create_from_escaped_tokens(
          descriptorTokens, descriptorNamespace);
      switch (column.type) {
        case ColumnType::String:
          columnDescriptor->set_matching_types(
              LiteralType::ClpStringT | LiteralType::VarStringT);
          break;
        case ColumnType::Integer:
          columnDescriptor->set_matching_types(
              LiteralType::IntegerT | LiteralType::TimestampT);
          break;
        case ColumnType::Float:
          columnDescriptor->set_matching_types(LiteralType::FloatT);
          break;
        case ColumnType::Boolean:
          columnDescriptor->set_matching_types(LiteralType::BooleanT);
          break;
        case ColumnType::Array:
          columnDescriptor->set_matching_types(LiteralType::ArrayT);
          break;
        case ColumnType::Timestamp:
          columnDescriptor->set_matching_types(
              LiteralType::TimestampT | LiteralType::IntegerT |
              LiteralType::FloatT);
          break;
        default:
          break;
      }

      projection_->add_column(columnDescriptor);
    }
  } catch (TraceableException& e) {
    VLOG(2) << e.what();
    return ErrorCode::InternalError;
  }
  projection_->resolve_columns(schemaTree);

  archiveReader_->read_metadata();

  matchedSchemas_.clear();
  for (auto schemaId : archiveReader_->get_schema_ids()) {
    if (schemaMatch_->schema_matched(schemaId)) {
      matchedSchemas_.push_back(schemaId);
    }
  }

  if (matchedSchemas_.empty()) {
    return ErrorCode::SchemaNotFound;
  }

  EvaluateTimestampIndex timestamp_index(timestampDict);
  if (EvaluatedValue::False == timestamp_index.run(expr_)) {
    VLOG(2) << "No matching timestamp ranges for query '" << query_ << "'";
    return ErrorCode::InvalidTimestampRange;
  }

  archiveReader_->read_variable_dictionary();
  archiveReader_->read_log_type_dictionary();
  archiveReader_->read_array_dictionary();

  currentSchemaIndex_ = 0;
  currentSchemaTableLoaded_ = false;

  archiveReader_->open_packed_streams();
  currentSplitLoaded_ = true;
  queryRunner_ = std::make_shared<ClpQueryRunner>(
      schemaMatch_, expr_, archiveReader_, false, projection_);
  queryRunner_->global_init();
  return ErrorCode::Success;
}

VectorPtr ClpArchiveCursor::createVectorHelper(
    memory::MemoryPool* pool,
    const TypePtr& vectorType,
    size_t vectorSize,
    const std::vector<clp_s::BaseColumnReader*>& projectedColumns) {
  if (vectorType->kind() == TypeKind::ROW) {
    auto& rowType = vectorType->as<TypeKind::ROW>();
    const uint32_t numChildren = rowType.size();
    std::vector<VectorPtr> children;
    children.reserve(numChildren);

    for (uint32_t i = 0; i < numChildren; ++i) {
      children.push_back(createVectorHelper(
          pool, rowType.childAt(i), vectorSize, projectedColumns));
    }
    return std::make_shared<RowVector>(
        pool, vectorType, nullptr, vectorSize, std::move(children));
  }
  auto vector = BaseVector::create(vectorType, vectorSize, pool);
  vector->setNulls(allocateNulls(vectorSize, pool, bits::kNull));

  if (metadataColumnIndices_.contains(columnIndex_)) {
    auto metadataVector =
        createPerFileMetadataVector(vectorType, vectorSize, pool);
    ++columnIndex_;
    if (metadataVector != nullptr) {
      return metadataVector;
    }
    // Metadata value not found — return null vector.
    return vector;
  }

  if (jsonStringColumnIndices_.contains(columnIndex_)) {
    ++columnIndex_;
    return std::make_shared<LazyVector>(
        pool,
        vectorType,
        vectorSize,
        std::make_unique<ClpArchiveJsonStringVectorLoader>(
            schemaReader_, filteredRowIndices_),
        std::move(vector));
  }

  VELOX_CHECK_LT(
      projectedColumnIndex_,
      projectedColumns.size(),
      "Projected column index out of bounds");
  auto* projectedColumn = projectedColumns[projectedColumnIndex_];
  auto projectedType = outputColumns_[columnIndex_].type;
  projectedColumnIndex_++;
  columnIndex_++;
  return std::make_shared<LazyVector>(
      pool,
      vectorType,
      vectorSize,
      std::make_unique<ClpArchiveVectorLoader>(
          projectedColumn, projectedType, filteredRowIndices_),
      std::move(vector));
}

bool ClpArchiveCursor::isMetadataColumn(const std::string& columnName) const {
  for (auto const& [irPath, columnValues] : perFileMetadataColumnValues_) {
    if (columnValues.count(columnName) > 0) {
      return true;
    }
  }
  return false;
}

void ClpArchiveCursor::setMetadataValue(
    BaseVector* vector,
    size_t i,
    const MetadataValueType& value) {
  std::visit(
      [&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
          vector->asFlatVector<StringView>()->set(
              i, StringView(v.data(), v.size()));
        } else if constexpr (std::is_same_v<T, int64_t>) {
          vector->asFlatVector<int64_t>()->set(i, v);
        } else if constexpr (std::is_same_v<T, double>) {
          vector->asFlatVector<double>()->set(i, v);
        }
      },
      value);
  vector->setNull(i, false);
}

VectorPtr ClpArchiveCursor::createPerFileMetadataVector(
    const TypePtr& vectorType,
    size_t vectorSize,
    memory::MemoryPool* pool) {
  if (perFileMetadataColumnValues_.empty() || filteredRowIndices_->empty()) {
    return nullptr;
  }

  auto columnName = outputColumns_[columnIndex_].name;

  auto vector = BaseVector::create(vectorType, vectorSize, pool);
  vector->setNulls(allocateNulls(vectorSize, pool, bits::kNull));

  for (size_t i = 0; i < vectorSize; ++i) {
    auto messageIndex = filteredRowIndices_->at(i);
    auto logEventIdx = queryRunner_->getLogEventId(messageIndex);

    try {
      auto const& rangeMetadata =
          archiveReader_->get_metadata_for_log_event(logEventIdx);

      // skip rows without _file_name metadata field.
      auto filenameIt =
          rangeMetadata.find(clp_s::constants::range_index::cFilename);
      if (filenameIt == rangeMetadata.end() || !filenameIt->is_string()) {
        continue;
      }
      auto const& irPath = filenameIt->get_ref<std::string const&>();

      // Skip rows from IR files that have no entry in the metadata projection
      // map.
      auto fileIt = perFileMetadataColumnValues_.find(irPath);
      if (fileIt == perFileMetadataColumnValues_.end()) {
        continue;
      }

      // Skip rows for which this column was not provided in the per-file map.
      auto colIt = fileIt->second.find(columnName);
      if (colIt == fileIt->second.end()) {
        continue;
      }

      setMetadataValue(vector.get(), i, colIt->second);
    } catch (std::exception const& e) {
      VLOG(2) << "Failed to get metadata for log event " << logEventIdx << ": "
              << e.what();
      // Leave as null.
    }
  }
  return vector;
}

} // namespace facebook::velox::connector::clp::search_lib
