// 指标注册表单元测试（阶段十三）。纯内存实现。
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/common/metrics.h"

namespace lan_file_hub {
namespace {

TEST(MetricsTest, CounterRendersWithLabelsAndHelp) {
  MetricsRegistry registry;
  registry.IncrementCounter("lan_file_hub_http_requests_total", "HTTP 请求总数",
                            {{"method", "GET"}, {"status", "200"}});
  registry.IncrementCounter("lan_file_hub_http_requests_total", "HTTP 请求总数",
                            {{"method", "GET"}, {"status", "200"}});
  const std::string rendered = registry.Render();
  EXPECT_NE(rendered.find("# TYPE lan_file_hub_http_requests_total counter"), std::string::npos);
  EXPECT_NE(rendered.find("lan_file_hub_http_requests_total{method=\"GET\",status=\"200\"} 2"),
            std::string::npos);
}

TEST(MetricsTest, GaugeKeepsLastValue) {
  MetricsRegistry registry;
  registry.SetGauge("lan_file_hub_active_uploads", "当前活跃上传会话", 3);
  registry.SetGauge("lan_file_hub_active_uploads", "当前活跃上传会话", 7);
  const std::string rendered = registry.Render();
  EXPECT_NE(rendered.find("# TYPE lan_file_hub_active_uploads gauge"), std::string::npos);
  EXPECT_NE(rendered.find("lan_file_hub_active_uploads 7"), std::string::npos);
  EXPECT_EQ(rendered.find("lan_file_hub_active_uploads 3"), std::string::npos);
}

TEST(MetricsTest, HistogramRendersBucketsSumAndCount) {
  MetricsRegistry registry;
  const std::vector<double> buckets{0.1, 0.5, 1.0};
  registry.ObserveHistogram("lan_file_hub_http_request_duration_seconds", "请求耗时", 0.05, {},
                            buckets);
  registry.ObserveHistogram("lan_file_hub_http_request_duration_seconds", "请求耗时", 0.3, {},
                            buckets);
  registry.ObserveHistogram("lan_file_hub_http_request_duration_seconds", "请求耗时", 5.0, {},
                            buckets);
  const std::string rendered = registry.Render();
  EXPECT_NE(rendered.find("lan_file_hub_http_request_duration_seconds_bucket{le=\"0.1\"} 1"),
            std::string::npos);
  EXPECT_NE(rendered.find("lan_file_hub_http_request_duration_seconds_bucket{le=\"0.5\"} 2"),
            std::string::npos);
  EXPECT_NE(rendered.find("lan_file_hub_http_request_duration_seconds_bucket{le=\"1\"} 2"),
            std::string::npos);
  EXPECT_NE(rendered.find("lan_file_hub_http_request_duration_seconds_bucket{le=\"+Inf\"} 3"),
            std::string::npos);
  EXPECT_NE(rendered.find("lan_file_hub_http_request_duration_seconds_count 3"), std::string::npos);
  EXPECT_NE(rendered.find("lan_file_hub_http_request_duration_seconds_sum 5.35"),
            std::string::npos);
}

TEST(MetricsTest, HistogramBucketsAccumulateWithLabels) {
  MetricsRegistry registry;
  const std::vector<double> buckets{1.0};
  registry.ObserveHistogram("m", "h", 0.4, {{"route", "/api/v1/files/{id}"}}, buckets);
  const std::string rendered = registry.Render();
  EXPECT_NE(rendered.find("m_bucket{le=\"1\",route=\"/api/v1/files/{id}\"} 1"), std::string::npos);
  EXPECT_NE(rendered.find("m_count{route=\"/api/v1/files/{id}\"} 1"), std::string::npos);
}

TEST(MetricsTest, LabelValuesAreEscaped) {
  MetricsRegistry registry;
  registry.IncrementCounter("lan_file_hub_errors_total", "错误数",
                            {{"code", "WEIRD\"CODE"}, {"note", "line1\nline2"}});
  const std::string rendered = registry.Render();
  EXPECT_NE(rendered.find("code=\"WEIRD\\\"CODE\""), std::string::npos) << "引号必须转义";
  EXPECT_NE(rendered.find("note=\"line1\\nline2\""), std::string::npos) << "换行必须转义";
}

TEST(MetricsTest, SeriesCardinalityIsBounded) {
  MetricsRegistry registry;
  for (int i = 0; i < 500; ++i) {
    registry.IncrementCounter("lan_file_hub_http_requests_total", "HTTP 请求总数",
                              {{"route", "/random/" + std::to_string(i)}});
  }
  EXPECT_EQ(registry.SeriesCount("lan_file_hub_http_requests_total"),
            MetricsRegistry::kMaxSeriesPerMetric)
      << "标签基数必须有硬上限，否则随机路径就能打爆内存";
  EXPECT_EQ(registry.dropped_series(), 500U - MetricsRegistry::kMaxSeriesPerMetric);
  EXPECT_NE(registry.Render().find("lan_file_hub_metrics_dropped_series_total"), std::string::npos)
      << "丢弃情况必须被自我暴露，否则标签设计错误会静默发生";
}

TEST(MetricsTest, InvalidMetricNameIsIgnored) {
  MetricsRegistry registry;
  registry.IncrementCounter("has space", "bad", {});
  registry.IncrementCounter("", "bad", {});
  EXPECT_EQ(registry.MetricCount(), 0U);
}

TEST(MetricsTest, SameNameWithDifferentTypeIsIgnored) {
  MetricsRegistry registry;
  registry.IncrementCounter("lan_file_hub_x", "counter", {});
  registry.SetGauge("lan_file_hub_x", "gauge", 5);  // 类型冲突：忽略
  const std::string rendered = registry.Render();
  EXPECT_NE(rendered.find("# TYPE lan_file_hub_x counter"), std::string::npos);
  EXPECT_EQ(rendered.find("lan_file_hub_x gauge"), std::string::npos);
}

TEST(MetricsTest, LabelsAreSortedForStableOutput) {
  MetricsRegistry registry;
  registry.IncrementCounter("m", "h", {{"b", "2"}, {"a", "1"}});
  EXPECT_NE(registry.Render().find("m{a=\"1\",b=\"2\"} 1"), std::string::npos)
      << "标签顺序必须稳定，否则同一序列会分裂成多条";
}

}  // namespace
}  // namespace lan_file_hub
