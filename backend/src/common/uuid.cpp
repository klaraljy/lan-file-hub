#include "lan_file_hub/common/uuid.h"

#include <array>
#include <cstdint>
#include <random>

namespace lan_file_hub {
namespace {

// 每个线程一份随机源：避免每次调用都重新播种，也避免全局锁。
std::mt19937_64& ThreadRandom() {
  static thread_local std::mt19937_64 engine(std::random_device{}());
  return engine;
}

}  // namespace

std::string GenerateUuidV4() {
  std::array<std::uint8_t, 16> bytes{};
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  for (auto& b : bytes) {
    b = static_cast<std::uint8_t>(dist(ThreadRandom()));
  }
  // 版本 4、变体 RFC 4122
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);

  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out.push_back('-');
    }
    out.push_back(kHex[bytes[i] >> 4]);
    out.push_back(kHex[bytes[i] & 0x0F]);
  }
  return out;
}

}  // namespace lan_file_hub
