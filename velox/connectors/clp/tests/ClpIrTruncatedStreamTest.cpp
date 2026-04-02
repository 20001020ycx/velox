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

class ClpIrTruncatedStreamTest : public exec::test::OperatorTestBase {
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

  exec::Split makeClpSplit(
      const std::string& splitPath,
      ClpConnectorSplit::SplitType type,
      std::shared_ptr<std::string> kqlQuery) {
    return exec::Split(std::make_shared<ClpConnectorSplit>(
        kClpConnectorId, splitPath, static_cast<int>(type), kqlQuery));
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

/// Verifies that a truncated IR stream returns partial results instead of
/// crashing. The truncated file is created by byte-level truncation of
/// test_1_ir.clp.zst (10 log events). After truncation, the CLP deserializer
/// should gracefully return whatever log events were fully deserialized before
/// hitting the IncompleteStream error.
TEST_F(ClpIrTruncatedStreamTest, testTruncatedIrStream) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(
              ROW({"requestId", "method"}, {VARCHAR(), VARCHAR()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_1"))
          .assignments({
              {"requestId",
               std::make_shared<ClpColumnHandle>(
                   "requestId", "requestId", VARCHAR())},
              {"method",
               std::make_shared<ClpColumnHandle>(
                   "method", "method", VARCHAR())},
          })
          .endTableScan()
          .planNode();

  RowVectorPtr output;
  ASSERT_NO_THROW(
      output = getResults(
          plan,
          {makeClpSplit(
              getExampleFilePath("error_handling/test_cockroach_1_ir_truncated.clp.zst"),
              ClpConnectorSplit::SplitType::kIr,
              kqlQuery)}));

  // The full file has 16,007 log events. The truncated file (70% of compressed
  // bytes) should yield 8,402 partial results.
  ASSERT_NE(output, nullptr);
  ASSERT_EQ(output->size(), 8402);
}

} // namespace
