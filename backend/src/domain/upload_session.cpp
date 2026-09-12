#include "lan_file_hub/domain/upload_session.h"

#include <stdexcept>

namespace lan_file_hub {

std::string ToDbString(UploadStatus status) {
  switch (status) {
    case UploadStatus::kUploading:
      return "UPLOADING";
    case UploadStatus::kMerging:
      return "MERGING";
    case UploadStatus::kVerifying:
      return "VERIFYING";
    case UploadStatus::kCompleted:
      return "COMPLETED";
    case UploadStatus::kFailed:
      return "FAILED";
    case UploadStatus::kAborted:
      return "ABORTED";
    case UploadStatus::kExpired:
      return "EXPIRED";
    case UploadStatus::kInitiated:
    default:
      return "INITIATED";
  }
}

UploadStatus ParseUploadStatus(const std::string& value) {
  if (value == "INITIATED")
    return UploadStatus::kInitiated;
  if (value == "UPLOADING")
    return UploadStatus::kUploading;
  if (value == "MERGING")
    return UploadStatus::kMerging;
  if (value == "VERIFYING")
    return UploadStatus::kVerifying;
  if (value == "COMPLETED")
    return UploadStatus::kCompleted;
  if (value == "FAILED")
    return UploadStatus::kFailed;
  if (value == "ABORTED")
    return UploadStatus::kAborted;
  if (value == "EXPIRED")
    return UploadStatus::kExpired;
  throw std::invalid_argument("未知的上传状态: " + value);
}

bool IsInProgress(UploadStatus status) {
  return status == UploadStatus::kInitiated || status == UploadStatus::kUploading;
}

bool IsTerminal(UploadStatus status) {
  return status == UploadStatus::kCompleted || status == UploadStatus::kAborted ||
         status == UploadStatus::kExpired || status == UploadStatus::kFailed;
}

std::uint32_t ComputeTotalChunks(std::uint64_t total_size, std::uint32_t chunk_size) {
  if (chunk_size == 0) {
    return 0;
  }
  if (total_size == 0) {
    return 1;  // 空文件也算一块
  }
  const std::uint64_t chunks = (total_size + chunk_size - 1) / chunk_size;
  if (chunks > 0xFFFFFFFFULL) {
    return 0;
  }
  return static_cast<std::uint32_t>(chunks);
}

Status ValidateChunkSize(std::uint32_t chunk_size) {
  if (chunk_size < kMinChunkSize || chunk_size > kMaxChunkSize) {
    return Status(Error{"INVALID_ARGUMENT", "分块大小必须在 " + std::to_string(kMinChunkSize) +
                                                " 到 " + std::to_string(kMaxChunkSize) +
                                                " 字节之间"});
  }
  return Status::Success();
}

}  // namespace lan_file_hub
