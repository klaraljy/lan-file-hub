// 日志脱敏单元测试（阶段十二第 15 条）。
// 关键点：脱敏不能只靠"每个调用点自己记得"，sink 层必须有最后一道闸。
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "lan_file_hub/common/logging.h"

namespace lan_file_hub {
namespace {

// 把写下来的文本留下来，用来断言脱敏效果
class CapturingSink : public spdlog::sinks::base_sink<std::mutex> {
 public:
  [[nodiscard]] const std::vector<std::string>& messages() const { return messages_; }

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    messages_.emplace_back(msg.payload.data(), msg.payload.size());
  }
  void flush_() override {}

 private:
  std::vector<std::string> messages_;
};

TEST(LoggingRedactionTest, RedactsSecretsEvenWhenTheCallSiteForgets) {
  auto capture = std::make_shared<CapturingSink>();
  auto sink = MakeRedactingSink(capture, {std::string("s3cr3t-db-password")});
  ASSERT_NE(sink, nullptr);
  spdlog::logger logger("test-redact", sink);

  logger.info("连接失败 | dsn=postgresql://app:s3cr3t-db-password@db:5432/app");

  ASSERT_EQ(capture->messages().size(), 1U);
  EXPECT_EQ(capture->messages()[0].find("s3cr3t-db-password"), std::string::npos)
      << "口令不得出现在日志里";
  EXPECT_NE(capture->messages()[0].find("***"), std::string::npos) << "要留下被替换的痕迹";
}

TEST(LoggingRedactionTest, RedactsEveryOccurrenceAndEverySecret) {
  auto capture = std::make_shared<CapturingSink>();
  auto sink =
      MakeRedactingSink(capture, {std::string("first-secret"), std::string("second-secret")});
  spdlog::logger logger("test-redact-multi", sink);

  logger.warn("a=first-secret b=second-secret c=first-secret");

  ASSERT_EQ(capture->messages().size(), 1U);
  const std::string& text = capture->messages()[0];
  EXPECT_EQ(text.find("first-secret"), std::string::npos);
  EXPECT_EQ(text.find("second-secret"), std::string::npos);
  EXPECT_NE(text.find("a=***"), std::string::npos);
}

TEST(LoggingRedactionTest, PassesThroughWhenNoSecretsConfigured) {
  auto capture = std::make_shared<CapturingSink>();
  auto sink = MakeRedactingSink(capture, {});
  EXPECT_EQ(sink, capture) << "没有敏感串时不该多包一层";
  spdlog::logger logger("test-redact-none", sink);
  logger.info("普通日志");
  ASSERT_EQ(capture->messages().size(), 1U);
  EXPECT_EQ(capture->messages()[0], "普通日志");
}

TEST(LoggingRedactionTest, VeryShortSecretsAreNotReplaced) {
  // 和 RedactSecrets 的既有语义一致：太短的串替换会把正常文本打烂
  auto capture = std::make_shared<CapturingSink>();
  auto sink = MakeRedactingSink(capture, {std::string("ab")});
  spdlog::logger logger("test-redact-short", sink);
  logger.info("aabbcc");
  ASSERT_EQ(capture->messages().size(), 1U);
  EXPECT_EQ(capture->messages()[0], "aabbcc");
}

}  // namespace
}  // namespace lan_file_hub
