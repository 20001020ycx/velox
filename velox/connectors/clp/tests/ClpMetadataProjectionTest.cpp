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

#include <gtest/gtest.h>

#include "velox/common/base/Fs.h"
#include "velox/connectors/clp/ClpColumnHandle.h"
#include "velox/connectors/clp/ClpConnector.h"
#include "velox/connectors/clp/ClpConnectorSplit.h"
#include "velox/connectors/clp/ClpTableHandle.h"
#include "velox/exec/tests/utils/AssertQueryBuilder.h"
#include "velox/exec/tests/utils/OperatorTestBase.h"
#include "velox/exec/tests/utils/PlanBuilder.h"

namespace {

using namespace facebook::velox;
using namespace facebook::velox::connector::clp;
using facebook::velox::exec::test::PlanBuilder;

class ClpMetadataProjectionTest : public exec::test::OperatorTestBase {
 public:
  const std::string kClpConnectorId = "test-clp";

  void SetUp() override {
    OperatorTestBase::SetUp();
    connector::clp::ClpConnectorFactory factory;
    auto clpConnector = factory.newConnector(
        kClpConnectorId,
        std::make_shared<const config::ConfigBase>(
            std::unordered_map<std::string, std::string>{}),
        nullptr,
        nullptr);
    connector::registerConnector(clpConnector);
  }

  void TearDown() override {
    connector::unregisterConnector(kClpConnectorId);
    OperatorTestBase::TearDown();
  }

  /// Creates a CLP split with metadata column values for testing metadata
  /// projection. The flat column-value map is wrapped in a single per-file
  /// entry keyed by splitPath, as expected by the IR cursor.
  exec::Split makeClpSplitWithMetadata(
      const std::string& splitPath,
      ClpConnectorSplit::SplitType type,
      std::shared_ptr<std::string> kqlQuery,
      std::map<std::string, MetadataValueType> metadataValues) {
    PerFileMetadataProjectionMap metadataProjection;
    metadataProjection.emplace(splitPath, std::move(metadataValues));
    return exec::Split(std::make_shared<ClpConnectorSplit>(
        kClpConnectorId,
        splitPath,
        static_cast<int>(type),
        kqlQuery,
        std::make_shared<PerFileMetadataProjectionMap>(std::move(metadataProjection))));
  }

  RowVectorPtr getResults(
      const core::PlanNodePtr& planNode,
      std::vector<exec::Split>&& splits) {
    return exec::test::AssertQueryBuilder(planNode)
        .splits(std::move(splits))
        .copyResults(pool());
  }

  static std::string getExampleFilePath(const std::string& filePath) {
    std::string current_path = fs::current_path().string();
    return current_path + "/examples/" + filePath;
  }
};

/**
 * Tests metadata projection by injecting constant values for columns that are
 * pre-fetched from the metadata database. This test validates that:
 * 1. String metadata values are correctly projected as constant vectors
 * 2. Integer metadata values are correctly projected as constant vectors
 * 3. Double metadata values are correctly projected as constant vectors
 * 4. Metadata projection works with IR split type
 * 5. Metadata columns are combined correctly with regular data columns
 */
TEST_F(ClpMetadataProjectionTest, metadataProjection) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;

  // Define metadata values of different types
  std::map<std::string, MetadataValueType> metadataValues = {
      {"source_file", std::string("/var/log/app.log")},
      {"partition_id", static_cast<int64_t>(42)},
      {"sampling_rate", 0.75}};

  auto plan = PlanBuilder()
                  .startTableScan()
                  .outputType(ROW(
                      {"requestId",
                       "method",
                       "source_file",
                       "partition_id",
                       "sampling_rate"},
                      {VARCHAR(), VARCHAR(), VARCHAR(), BIGINT(), DOUBLE()}))
                  .tableHandle(std::make_shared<ClpTableHandle>(
                      kClpConnectorId, "test_1"))
                  .assignments(
                      {{"requestId",
                        std::make_shared<ClpColumnHandle>(
                            "requestId", "requestId", VARCHAR())},
                       {"method",
                        std::make_shared<ClpColumnHandle>(
                            "method", "method", VARCHAR())},
                       {"source_file",
                        std::make_shared<ClpColumnHandle>(
                            "source_file", "source_file", VARCHAR())},
                       {"partition_id",
                        std::make_shared<ClpColumnHandle>(
                            "partition_id", "partition_id", BIGINT())},
                       {"sampling_rate",
                        std::make_shared<ClpColumnHandle>(
                            "sampling_rate", "sampling_rate", DOUBLE())}})
                  .endTableScan()
                  .planNode();

  auto output = getResults(
      plan,
      {makeClpSplitWithMetadata(
          getExampleFilePath("test_1_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery,
          metadataValues)});

  auto expected = makeRowVector(
      {// requestId (from data)
       makeFlatVector<StringView>({
           "req-100",
           "req-101",
           "req-102",
           "req-103",
           "req-104",
           "req-105",
           "req-106",
           "req-107",
           "req-108",
           "req-109",
       }),
       // method (from data)
       makeFlatVector<StringView>({
           "GET",
           "POST",
           "GET",
           "PUT",
           "DELETE",
           "GET",
           "POST",
           "GET",
           "PATCH",
           "GET",
       }),
       // source_file (metadata - string constant)
       makeFlatVector<StringView>(
           10, [](auto /* row */) { return "/var/log/app.log"; }),
       // partition_id (metadata - int64 constant)
       makeFlatVector<int64_t>(10, [](auto /* row */) { return 42; }),
       // sampling_rate (metadata - double constant)
       makeFlatVector<double>(10, [](auto /* row */) { return 0.75; })});

  test::assertEqualVectors(expected, output);
}

/**
 * Tests that metadata columns take precedence over data columns when a column
 * name exists in both sources.
 */
TEST_F(ClpMetadataProjectionTest, metadataProjectionPrecedence) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;

  // Define metadata value for "method" column which also exists in the data.
  // In the data, method has values like "GET", "POST", "PUT", etc.
  // We override it with a constant metadata value.
  std::map<std::string, MetadataValueType> metadataValues = {
      {"method", std::string("METADATA_OVERRIDE")}};

  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(ROW({"requestId", "method"}, {VARCHAR(), VARCHAR()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_1"))
          .assignments(
              {{"requestId",
                std::make_shared<ClpColumnHandle>(
                    "requestId", "requestId", VARCHAR())},
               {"method",
                std::make_shared<ClpColumnHandle>(
                    "method", "method", VARCHAR())}})
          .endTableScan()
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplitWithMetadata(
          getExampleFilePath("test_1_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery,
          metadataValues)});

  // Expected: method column should have the metadata value "METADATA_OVERRIDE"
  // for all rows, NOT the actual data values ("GET", "POST", etc.)
  auto expected =
      makeRowVector({// requestId (from data)
                     makeFlatVector<StringView>({
                         "req-100",
                         "req-101",
                         "req-102",
                         "req-103",
                         "req-104",
                         "req-105",
                         "req-106",
                         "req-107",
                         "req-108",
                         "req-109",
                     }),
                     // method (from metadata - overrides data values)
                     makeFlatVector<StringView>(10, [](auto /* row */) {
                       return "METADATA_OVERRIDE";
                     })});

  test::assertEqualVectors(expected, output);
}

/**
 * Tests archive metadata projection where different IR files within the same
 * archive receive different metadata values. The archive
 * (test_multi_file.clps) was created from test_multi_file_a.ndjson (3 rows)
 * and test_multi_file_b.ndjson (3 rows), so the range index stores two
 * distinct _filename entries. This verifies that createPerFileMetadataVector
 * correctly applies per-file metadata by looking up the _filename from the
 * range index for each log event.
 */
TEST_F(ClpMetadataProjectionTest, archiveMetadataProjectionMultiFile) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;

  // Different metadata values for each source IR file in the archive.
  PerFileMetadataProjectionMap metadataProjection = {
      {"test_multi_file_a.ndjson",
       {{"source_file", std::string("file_a")},
        {"partition_id", static_cast<int64_t>(1)}}},
      {"test_multi_file_b.ndjson",
       {{"source_file", std::string("file_b")},
        {"partition_id", static_cast<int64_t>(2)}}}};

  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(ROW(
              {"requestId", "source_file", "partition_id"},
              {VARCHAR(), VARCHAR(), BIGINT()}))
          .tableHandle(std::make_shared<ClpTableHandle>(
              kClpConnectorId, "test_multi_file"))
          .assignments(
              {{"requestId",
                std::make_shared<ClpColumnHandle>(
                    "requestId", "requestId", VARCHAR())},
               {"source_file",
                std::make_shared<ClpColumnHandle>(
                    "source_file", "source_file", VARCHAR())},
               {"partition_id",
                std::make_shared<ClpColumnHandle>(
                    "partition_id", "partition_id", BIGINT())}})
          .endTableScan()
          .planNode();

  auto output = getResults(
      plan,
      {exec::Split(std::make_shared<ClpConnectorSplit>(
          kClpConnectorId,
          getExampleFilePath("metadata_projection/test_multi_file.clps"),
          static_cast<int>(ClpConnectorSplit::SplitType::kArchive),
          kqlQuery,
          std::make_shared<PerFileMetadataProjectionMap>(
              metadataProjection)))});

  // Rows from test_multi_file_a.ndjson (req-a*) come before rows from
  // test_multi_file_b.ndjson (req-b*) as stored in the archive.
  auto expected = makeRowVector(
      {makeFlatVector<StringView>(
           {"req-a1", "req-a2", "req-a3", "req-b1", "req-b2", "req-b3"}),
       makeFlatVector<StringView>(
           {"file_a", "file_a", "file_a", "file_b", "file_b", "file_b"}),
       makeFlatVector<int64_t>({1, 1, 1, 2, 2, 2})});

  test::assertEqualVectors(expected, output);
}

/**
 * Tests archive metadata projection with multiple schema tables. The archive
 * (test_multi_schema.clps) was created from two source files, each containing
 * logs with two distinct schemas (identified by the "schema" data column):
 *   - Schema 1 (schema="service_schema"): {requestId, schema, service, status}
 *   - Schema 2 (schema="message_schema"): {requestId, schema, message}
 *
 * CLP-S groups log events by schema, so rows from schema 2 end up in a
 * non-first schema table. The log_event_idx column stores global log event
 * indices that must be correctly translated to look up the right _filename in
 * the range index. If global-to-local translation is broken, rows in schema
 * table 2 would get wrong or missing metadata values.
 */
TEST_F(ClpMetadataProjectionTest, archiveMetadataProjectionMultiSchema) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;

  PerFileMetadataProjectionMap metadataProjection = {
      {"test_multi_schema_a.ndjson",
       {{"source_file", std::string("file_a")},
        {"partition_id", static_cast<int64_t>(1)}}},
      {"test_multi_schema_b.ndjson",
       {{"source_file", std::string("file_b")},
        {"partition_id", static_cast<int64_t>(2)}}}};

  // Project the "schema" data column alongside metadata columns so the output
  // clearly shows which CLP-S schema table each row came from ("svc" vs "msg").
  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(ROW(
              {"requestId", "schema", "source_file", "partition_id"},
              {VARCHAR(), VARCHAR(), VARCHAR(), BIGINT()}))
          .tableHandle(std::make_shared<ClpTableHandle>(
              kClpConnectorId, "test_multi_schema"))
          .assignments(
              {{"requestId",
                std::make_shared<ClpColumnHandle>(
                    "requestId", "requestId", VARCHAR())},
               {"schema",
                std::make_shared<ClpColumnHandle>(
                    "schema", "schema", VARCHAR())},
               {"source_file",
                std::make_shared<ClpColumnHandle>(
                    "source_file", "source_file", VARCHAR())},
               {"partition_id",
                std::make_shared<ClpColumnHandle>(
                    "partition_id", "partition_id", BIGINT())}})
          .endTableScan()
          .planNode();

  auto output = getResults(
      plan,
      {exec::Split(std::make_shared<ClpConnectorSplit>(
          kClpConnectorId,
          getExampleFilePath("metadata_projection/test_multi_schema.clps"),
          static_cast<int>(ClpConnectorSplit::SplitType::kArchive),
          kqlQuery,
          std::make_shared<PerFileMetadataProjectionMap>(
              metadataProjection)))});

  // CLP-S groups by schema: "service_schema" rows come first, then
  // "message_schema" rows. The "schema" data column makes this grouping
  // visible. The global log_event_idx must be correctly resolved for rows in
  // the second schema table to get the right _filename and metadata values.
  //
  // requestId  schema          source_file  partition_id
  // ---------  --------------  -----------  ------------
  // req-a1     service_schema  file_a       1     <- schema table 1, file_a
  // req-a3     service_schema  file_a       1
  // req-a5     service_schema  file_a       1
  // req-b1     service_schema  file_b       2     <- schema table 1, file_b
  // req-b3     service_schema  file_b       2
  // req-b5     service_schema  file_b       2
  // req-a2     message_schema  file_a       1     <- schema table 2, file_a
  // req-a6     message_schema  file_a       1
  // req-b2     message_schema  file_b       2     <- schema table 2, file_b
  // req-b4     message_schema  file_b       2
  // req-b6     message_schema  file_b       2
  // req-a4     message_schema  file_a       1     <- schema table 2, file_a
  auto expected = makeRowVector(
      {makeFlatVector<StringView>(
           {"req-a1",
            "req-a3",
            "req-a5",
            "req-b1",
            "req-b3",
            "req-b5",
            "req-a2",
            "req-a6",
            "req-b2",
            "req-b4",
            "req-b6",
            "req-a4"}),
       makeFlatVector<StringView>(
           {"service_schema", "service_schema", "service_schema",
            "service_schema", "service_schema", "service_schema",
            "message_schema", "message_schema", "message_schema",
            "message_schema", "message_schema", "message_schema"}),
       makeFlatVector<StringView>(
           {"file_a",
            "file_a",
            "file_a",
            "file_b",
            "file_b",
            "file_b",
            "file_a",
            "file_a",
            "file_b",
            "file_b",
            "file_b",
            "file_a"}),
       makeFlatVector<int64_t>({1, 1, 1, 2, 2, 2, 1, 1, 2, 2, 2, 1})});

  test::assertEqualVectors(expected, output);
}

} // namespace
