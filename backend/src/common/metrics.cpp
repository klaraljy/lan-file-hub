#include "lan_file_hub/common/metrics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace lan_file_hub {
namespace {

// 指标名只允许 Prometheus 的合法字符集；不合法直接丢弃，避免调用点写错还暴露出去
bool IsValidMetricName(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  return std::ranges::all_of(name, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == ':';
  });
}

std::string EscapeLabelValue(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        break;  // 回车直接丢掉
      default:
        escaped += c;
    }
  }
  return escaped;
}

// 去掉定点打印留下的尾随 0（"5.350000" -> "5.35"）
std::string TrimTrailingZeros(std::string text) {
  if (text.find('.') == std::string::npos) {
    return text;
  }
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text;
}

// 计数器按整数打印；其它按定点打印（大数不要写成 6.19e+10，人也要能读）；
// 极小值保留科学计数法，否则会退化成 0.000000
std::string FormatValue(double value, bool as_integer) {
  if (as_integer) {
    return std::to_string(static_cast<std::uint64_t>(value));
  }
  const double magnitude = std::abs(value);
  if (magnitude > 0.0 && magnitude < 1e-4) {
    std::ostringstream scientific;
    scientific.precision(9);
    scientific << value;
    return scientific.str();
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  return TrimTrailingZeros(out.str());
}

std::string FormatBucketBound(double bound) {
  std::ostringstream out;
  out.precision(9);
  out << bound;
  return out.str();
}

}  // namespace

std::string RenderLabels(const MetricsRegistry::Labels& labels) {
  if (labels.empty()) {
    return {};
  }
  std::string rendered = "{";
  bool first = true;
  for (const auto& [key, value] : labels) {
    if (!first) {
      rendered += ",";
    }
    first = false;
    rendered += key;
    rendered += "=\"";
    rendered += EscapeLabelValue(value);
    rendered += "\"";
  }
  rendered += "}";
  return rendered;
}

std::vector<double> MetricsRegistry::DefaultDurationBuckets() {
  return std::vector<double>(std::begin(kDefaultDurationBuckets),
                             std::end(kDefaultDurationBuckets));
}

MetricsRegistry::Series* MetricsRegistry::FindOrCreateSeries(Metric& metric, const Labels& labels) {
  const std::string key = RenderLabels(labels);
  const auto found = metric.series.find(key);
  if (found != metric.series.end()) {
    return &found->second;
  }
  if (metric.series.size() >= kMaxSeriesPerMetric) {
    // 标签基数失控：丢新序列并计数，绝不无限增长
    dropped_series_.fetch_add(1);
    return nullptr;
  }
  Series series;
  if (metric.type == Type::kHistogram) {
    series.buckets.assign(metric.buckets.size(), 0);
  }
  return &metric.series.emplace(key, std::move(series)).first->second;
}

void MetricsRegistry::IncrementCounter(std::string_view name, std::string_view help,
                                       const Labels& labels, std::uint64_t amount) {
  if (!IsValidMetricName(name)) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  Metric& metric = metrics_[std::string(name)];
  if (metric.help.empty()) {
    metric.type = Type::kCounter;
    metric.help = std::string(help);
  }
  if (metric.type != Type::kCounter) {
    return;  // 同名不同类型：忽略，避免暴露格式错乱
  }
  if (Series* series = FindOrCreateSeries(metric, labels); series != nullptr) {
    series->count += amount;
    series->value = static_cast<double>(series->count);
  }
}

void MetricsRegistry::SetGauge(std::string_view name, std::string_view help, double value,
                               const Labels& labels) {
  if (!IsValidMetricName(name)) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  Metric& metric = metrics_[std::string(name)];
  if (metric.help.empty()) {
    metric.type = Type::kGauge;
    metric.help = std::string(help);
  }
  if (metric.type != Type::kGauge) {
    return;
  }
  if (Series* series = FindOrCreateSeries(metric, labels); series != nullptr) {
    series->value = value;
  }
}

void MetricsRegistry::ObserveHistogram(std::string_view name, std::string_view help, double value,
                                       const Labels& labels, const std::vector<double>& buckets) {
  if (!IsValidMetricName(name)) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  Metric& metric = metrics_[std::string(name)];
  if (metric.help.empty()) {
    metric.type = Type::kHistogram;
    metric.help = std::string(help);
    metric.buckets = buckets.empty() ? DefaultDurationBuckets() : buckets;
  }
  if (metric.type != Type::kHistogram) {
    return;
  }
  Series* series = FindOrCreateSeries(metric, labels);
  if (series == nullptr) {
    return;
  }
  series->count += 1;
  series->sum += value;
  for (std::size_t i = 0; i < metric.buckets.size(); ++i) {
    if (value <= metric.buckets[i]) {
      series->buckets[i] += 1;
    }
  }
}

std::string MetricsRegistry::Render() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string out;
  for (const auto& [name, metric] : metrics_) {
    if (metric.series.empty()) {
      continue;
    }
    const char* type = metric.type == Type::kCounter ? "counter"
                       : metric.type == Type::kGauge ? "gauge"
                                                     : "histogram";
    out += "# HELP " + name + " " + metric.help + "\n";
    out += "# TYPE " + name + " " + type + "\n";
    for (const auto& [labels, series] : metric.series) {
      if (metric.type == Type::kHistogram) {
        std::uint64_t cumulative = 0;
        for (std::size_t i = 0; i < metric.buckets.size(); ++i) {
          cumulative = series.buckets[i];
          out += name + "_bucket{le=\"" + FormatBucketBound(metric.buckets[i]) + "\"";
          out += labels.empty() ? "" : "," + labels.substr(1, labels.size() - 2);
          out += "} " + std::to_string(cumulative) + "\n";
        }
        out += name + "_bucket{le=\"+Inf\"";
        out += labels.empty() ? "" : "," + labels.substr(1, labels.size() - 2);
        out += "} " + std::to_string(series.count) + "\n";
        out += name + "_sum" + labels + " " + FormatValue(series.sum, false) + "\n";
        out += name + "_count" + labels + " " + std::to_string(series.count) + "\n";
        continue;
      }
      const bool as_integer = metric.type == Type::kCounter;
      out += name + labels + " " + FormatValue(series.value, as_integer) + "\n";
    }
  }
  // 自我暴露：标签基数失控时能立刻看出来
  const auto dropped = dropped_series_.load();
  out += "# HELP lan_file_hub_metrics_dropped_series_total 因超过序列上限被丢弃的指标序列数\n";
  out += "# TYPE lan_file_hub_metrics_dropped_series_total counter\n";
  out += "lan_file_hub_metrics_dropped_series_total " + std::to_string(dropped) + "\n";
  return out;
}

std::size_t MetricsRegistry::MetricCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return metrics_.size();
}

std::size_t MetricsRegistry::SeriesCount(std::string_view name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = metrics_.find(std::string(name));
  return found == metrics_.end() ? 0 : found->second.series.size();
}

MetricsRegistry& Metrics() {
  static MetricsRegistry registry;
  return registry;
}

}  // namespace lan_file_hub
