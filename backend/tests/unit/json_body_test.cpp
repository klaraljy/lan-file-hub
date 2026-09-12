// JSON 请求体大小上限单元测试（阶段十二）。
// 纯内存实现，不依赖数据库与网络。
#include <string>

#include <drogon/HttpRequest.h>
#include <gtest/gtest.h>

#include "lan_file_hub/api/json_body.h"

namespace lan_file_hub {
namespace {

drogon::HttpRequestPtr MakeRequest(const std::string& body) {
  auto req = drogon::HttpRequest::newHttpRequest();
  req->setBody(body);
  return req;
}

std::string BodyOfSize(std::size_t size) {
  // 生成合法 JSON：{"pad":"xxx..."}
  const std::string prefix = "{\"pad\":\"";
  const std::string suffix = "\"}";
  const std::size_t pad =
      size > prefix.size() + suffix.size() ? size - prefix.size() - suffix.size() : 0;
  return prefix + std::string(pad, 'x') + suffix;
}

TEST(JsonBodyTest, ParsesSmallBody) {
  const auto parsed = ParseJsonBody(MakeRequest("{\"name\":\"docs\"}"));
  ASSERT_TRUE(parsed.Ok());
  EXPECT_EQ(parsed.Value()["name"], "docs");
}

TEST(JsonBodyTest, AcceptsBodyAtExactLimit) {
  const auto parsed = ParseJsonBody(MakeRequest(BodyOfSize(kMaxJsonBodyBytes)), kMaxJsonBodyBytes);
  ASSERT_TRUE(parsed.Ok());
  EXPECT_TRUE(parsed.Value().contains("pad"));
}

TEST(JsonBodyTest, RejectsBodyOverLimit) {
  const auto parsed = ParseJsonBody(MakeRequest(BodyOfSize(kMaxJsonBodyBytes + 1)));
  ASSERT_FALSE(parsed.Ok());
  EXPECT_EQ(parsed.GetError().code, "PAYLOAD_TOO_LARGE");
}

TEST(JsonBodyTest, RejectsFarOverLimitBody) {
  // 2 MiB 的合法 JSON：必须被大小检查挡住，不能进入解析
  const auto parsed = ParseJsonBody(MakeRequest(BodyOfSize(2 * 1024 * 1024)));
  ASSERT_FALSE(parsed.Ok());
  EXPECT_EQ(parsed.GetError().code, "PAYLOAD_TOO_LARGE");
}

TEST(JsonBodyTest, HonoursCustomLimit) {
  const auto parsed = ParseJsonBody(MakeRequest("{\"pad\":\"0123456789\"}"), 8);
  ASSERT_FALSE(parsed.Ok());
  EXPECT_EQ(parsed.GetError().code, "PAYLOAD_TOO_LARGE");
}

TEST(JsonBodyTest, RejectsInvalidJsonUnderLimit) {
  const auto parsed = ParseJsonBody(MakeRequest("not-json"));
  ASSERT_FALSE(parsed.Ok());
  EXPECT_EQ(parsed.GetError().code, "INVALID_ARGUMENT");
}

TEST(JsonBodyTest, RejectsEmptyBody) {
  const auto parsed = ParseJsonBody(MakeRequest(""));
  ASSERT_FALSE(parsed.Ok());
  EXPECT_EQ(parsed.GetError().code, "INVALID_ARGUMENT");
}

TEST(JsonBodyTest, SizeCheckAcceptsRealisticMetadataBody) {
  // 真实元数据请求体（几十个字段）远小于上限
  const auto status = CheckJsonBodySize(MakeRequest(BodyOfSize(8 * 1024)));
  EXPECT_TRUE(status.Ok());
}

TEST(JsonBodyTest, SizeCheckExplainsRejection) {
  const auto status = CheckJsonBodySize(MakeRequest(BodyOfSize(kMaxJsonBodyBytes + 1024)));
  ASSERT_FALSE(status.Ok());
  EXPECT_EQ(status.GetError().code, "PAYLOAD_TOO_LARGE");
  EXPECT_FALSE(status.GetError().detail.empty());
}

}  // namespace
}  // namespace lan_file_hub
