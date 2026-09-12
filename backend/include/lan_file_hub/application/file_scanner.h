// 病毒扫描抽象（阶段十二第 17 条）。
//
// 本期**没有**接入真实扫描引擎：只提供接口与 NoopFileScanner。
// 接入真实引擎（ClamAV 等）时实现本接口、在 main.cpp 换掉 NoopFileScanner 即可，
// 业务层（UploadService）不需要改动，README 里"无真实病毒扫描"的说明才需要同步更新。
#pragma once

#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

enum class ScanVerdict {
  kClean,     // 扫描通过
  kInfected,  // 判定为恶意内容：调用方必须拒绝引用该对象
  kSkipped,   // 没有扫描能力（本期）
};

struct ScanResult {
  ScanVerdict verdict{ScanVerdict::kSkipped};
  std::string detail;  // 只进日志，不进响应
};

class FileScanner {
 public:
  virtual ~FileScanner() = default;

  // 扫描一个已经落到最终位置的对象。
  // storage_key 是 ObjectStorage 里的 Key（实现可据此取到内容），不是用户可见路径。
  [[nodiscard]] virtual Result<ScanResult> Scan(std::string_view storage_key,
                                                std::string_view content_hash) = 0;

  // 扫描器名称，进日志与启动提示
  [[nodiscard]] virtual std::string_view name() const = 0;
};

// 本期实现：不扫描，明确返回 kSkipped。
// 刻意不返回 kClean —— 那等于假装做过安全检测（README 与 docs/security.md 有说明）。
class NoopFileScanner : public FileScanner {
 public:
  [[nodiscard]] Result<ScanResult> Scan(std::string_view storage_key,
                                        std::string_view content_hash) override;
  [[nodiscard]] std::string_view name() const override { return "noop"; }
};

}  // namespace lan_file_hub
