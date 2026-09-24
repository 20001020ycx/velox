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

#include <nlohmann/json.hpp>

#include "velox/connectors/clp/search_lib/ir/ClpIrJsonStringVectorLoader.h"

namespace facebook::velox::connector::clp::search_lib {
namespace {

// `\xe0` starts a 3-byte UTF-8 sequence but is followed by `\x01`, which is not
// a valid continuation byte. nlohmann's default (strict) serializer rejects
// this with `type_error.316`; see y-scope/uber-presto#43.
constexpr char kInvalidUtf8[] = "prefix\xe0\x01suffix";

// U+FFFD (REPLACEMENT CHARACTER) encoded as UTF-8; nlohmann's `replace` error
// handler emits it for each invalid byte in the sequence above.
constexpr char kReplacementChar[] = "\xef\xbf\xbd";

TEST(ClpIrJsonStringVectorLoaderTest, validUtf8IsSerializedUnchanged) {
  nlohmann::json json;
  json["message"] = "hello world";
  EXPECT_EQ(serializeLogEventJson(json), R"({"message":"hello world"})");
}

TEST(ClpIrJsonStringVectorLoaderTest, invalidUtf8DoesNotThrow) {
  nlohmann::json json;
  json["message"] = kInvalidUtf8;

  // Sanity check: the default serializer throws, which is the crash we fix.
  EXPECT_THROW(json.dump(), nlohmann::json::exception);

  std::string result;
  ASSERT_NO_THROW(result = serializeLogEventJson(json));

  // The result is valid UTF-8 (nlohmann re-validates on parse) with the
  // invalid bytes replaced by U+FFFD, and the surrounding text preserved.
  EXPECT_NO_THROW(nlohmann::json::parse(result));
  EXPECT_NE(result.find("prefix"), std::string::npos);
  EXPECT_NE(result.find("suffix"), std::string::npos);
  EXPECT_NE(result.find(kReplacementChar), std::string::npos);
}

} // namespace
} // namespace facebook::velox::connector::clp::search_lib
