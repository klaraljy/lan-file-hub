// 进程内指标注册表 + Prometheus 文本暴露（阶段十三）。
//
// 约束（任务书 §13）：**标签值不能直接使用无限制的用户输入**。
// 所以本注册表对每个指标的序列数设硬上限：超出上限的序列被丢弃并计入 dropped 计数——
// 宁可少一个标签组合，也不能让攻击者用随机 URL/ID 把指标内存打爆。
// 调用方还要自己保证：路由标签用路由模板（`getMatchedPathPattern()`），不要用原始路径。
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace lan_file_hub {

// 默认延迟桶（秒）：从局域网快请求到慢查询都有覆盖
inline constexpr double kDefaultDurationBuckets[] = {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25,
                                                     0.5,   1.0,   2.5,  5.0,   10.0, 30.0};

class MetricsRegistry {
 public:
  using Labels = std::map<std::string, std::string>;

  // 每个指标最多保留的序列数（一个标签组合 = 一条序列）
  static constexpr std::size_t kMaxSeriesPerMetric = 200;

  void IncrementCounter(std::string_view name, std::string_view help, const Labels& labels = {},
                        std::uint64_t amount = 1);

  void SetGauge(std::string_view name, std::string_view help, double value,
                const Labels& labels = {});

  void ObserveHistogram(std::string_view name, std::string_view help, double value,
                        const Labels& labels = {},
                        const std::vector<double>& buckets = DefaultDurationBuckets());

  // Prometheus 文本格式（text/plain; version=0.0.4）
  [[nodiscard]] std::string Render() const;

  // 因为超过序列上限而被丢弃的序列数（暴露成指标，便于发现标签设计错误）
  [[nodiscard]] std::uint64_t dropped_series() const { return dropped_series_.load(); }

  [[nodiscard]] std::size_t MetricCount() const;
  [[nodiscard]] std::size_t SeriesCount(std::string_view name) const;

  [[nodiscard]] static std::vector<double> DefaultDurationBuckets();

 private:
  enum class Type { kCounter, kGauge, kHistogram };

  struct Series {
    std::uint64_t count{0};              // counter / histogram 样本数
    double sum{0.0};                     // histogram 观测值之和
    double value{0.0};                   // gauge 当前值
    std::vector<std::uint64_t> buckets;  // histogram 各桶累计计数
  };

  struct Metric {
    Type type{Type::kCounter};
    std::string help;
    std::vector<double> buckets;           // histogram 桶上界
    std::map<std::string, Series> series;  // key = 已渲染的标签串（顺序稳定）
  };

  // 返回 nullptr 表示被上限丢弃
  Series* FindOrCreateSeries(Metric& metric, const Labels& labels);

  std::map<std::string, Metric> metrics_;
  mutable std::mutex mutex_;
  std::atomic<std::uint64_t> dropped_series_{0};
};

// 进程级注册表。指标不是业务数据，全局单例是标准做法（见 docs/architecture.md）
[[nodiscard]] MetricsRegistry& Metrics();

// 把标签渲染成 Prometheus 的 {a="b",c="d"} 形式；空标签返回空串
[[nodiscard]] std::string RenderLabels(const MetricsRegistry::Labels& labels);

}  // namespace lan_file_hub
