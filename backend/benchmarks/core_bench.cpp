// 微基准（阶段十四第 19 条）：CPU/磁盘热点路径的吞吐，不做网络请求。
//
// 环境与复现方式见 docs/performance.md。数据集固定（固定大小的确定性数据），
// 便于不同机器/不同提交之间对比。
#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/storage/local_object_storage.h"

#include "fixtures/temp_dir.h"

namespace lan_file_hub {
namespace {

constexpr std::size_t kMiB = 1024 * 1024;

// 固定数据集：用固定种子生成，保证可复现
std::string MakePayload(std::size_t size) {
  std::string payload(size, '\0');
  std::uint32_t state = 0x12345678U;
  for (std::size_t i = 0; i < size; ++i) {
    state = state * 1664525U + 1013904223U;  // 线性同余，确定性
    payload[i] = static_cast<char>((state >> 16) & 0xFF);
  }
  return payload;
}

std::shared_ptr<LocalObjectStorage> MakeStorage(testing::TempDir& dir) {
  auto storage = LocalObjectStorage::Create(dir.Path());
  if (!storage.Ok()) {
    return nullptr;
  }
  return storage.Value();
}

void BM_Sha256_64MiB(benchmark::State& state) {
  const std::string payload = MakePayload(64 * kMiB);
  for (auto _ : state) {
    std::string digest = Sha256Hex(payload);
    benchmark::DoNotOptimize(digest);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(payload.size()));
}
BENCHMARK(BM_Sha256_64MiB)->Unit(benchmark::kMillisecond);

void BM_ObjectStorageWrite_64MiB(benchmark::State& state) {
  testing::TempDir dir{"bench_storage"};
  auto storage = MakeStorage(dir);
  if (storage == nullptr) {
    state.SkipWithError("对象存储不可用");
    return;
  }
  const std::string payload = MakePayload(64 * kMiB);
  for (auto _ : state) {
    auto writer = storage->CreateWriter();
    if (!writer.Ok()) {
      state.SkipWithError("无法创建写入器");
      return;
    }
    // 按 1 MiB 写入：与真实分块落盘粒度接近
    for (std::size_t offset = 0; offset < payload.size(); offset += kMiB) {
      const std::size_t length = std::min(kMiB, payload.size() - offset);
      const auto written = writer.Value()->Write(payload.data() + offset, length);
      if (!written.Ok()) {
        state.SkipWithError("写入失败");
        return;
      }
    }
    if (!writer.Value()->Finish().Ok()) {
      state.SkipWithError("落盘失败");
      return;
    }
    (void)storage->Delete(writer.Value()->temp_key());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(payload.size()));
}
BENCHMARK(BM_ObjectStorageWrite_64MiB)->Unit(benchmark::kMillisecond)->UseRealTime();

void BM_ObjectStorageRead_64MiB(benchmark::State& state) {
  testing::TempDir dir{"bench_storage_read"};
  auto storage = MakeStorage(dir);
  if (storage == nullptr) {
    state.SkipWithError("对象存储不可用");
    return;
  }
  const std::string payload = MakePayload(64 * kMiB);
  auto writer = storage->CreateWriter();
  if (!writer.Ok() || !writer.Value()->Write(payload.data(), payload.size()).Ok() ||
      !writer.Value()->Finish().Ok()) {
    state.SkipWithError("准备测试对象失败");
    return;
  }
  const std::string key = std::string(writer.Value()->temp_key());
  auto path = storage->ResolveDownloadPath(key);
  if (!path.Ok()) {
    state.SkipWithError("无法解析对象路径");
    return;
  }

  for (auto _ : state) {
    std::ifstream file(path.Value(), std::ios::binary);
    if (!file.is_open()) {
      state.SkipWithError("无法打开对象文件");
      return;
    }
    std::vector<char> buffer(kMiB);
    std::size_t total = 0;
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
           file.gcount() > 0) {
      total += static_cast<std::size_t>(file.gcount());
      benchmark::DoNotOptimize(buffer);
    }
    if (total != payload.size()) {
      state.SkipWithError("读取长度与预期不符");
      return;
    }
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(payload.size()));
}
BENCHMARK(BM_ObjectStorageRead_64MiB)->Unit(benchmark::kMillisecond)->UseRealTime();

}  // namespace
}  // namespace lan_file_hub

BENCHMARK_MAIN();
