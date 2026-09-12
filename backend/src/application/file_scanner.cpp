#include "lan_file_hub/application/file_scanner.h"

namespace lan_file_hub {

Result<ScanResult> NoopFileScanner::Scan(std::string_view storage_key,
                                         std::string_view content_hash) {
  (void)storage_key;
  (void)content_hash;
  ScanResult result;
  result.verdict = ScanVerdict::kSkipped;
  result.detail = "未接入扫描引擎（NoopFileScanner）";
  return result;
}

}  // namespace lan_file_hub
