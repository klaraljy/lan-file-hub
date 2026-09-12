// 端口自检工具实现：试着绑一次，绑不上就按错误码区分「被占用」与「被系统保留」。
#include "lan_file_hub/common/net.h"

#include <cerrno>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace lan_file_hub {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

void CloseSocket(SocketHandle socket) {
  ::closesocket(socket);
}

int LastSocketError() {
  return ::WSAGetLastError();
}

bool IsInUseError(int error) {
  return error == WSAEADDRINUSE;
}
bool IsForbiddenError(int error) {
  return error == WSAEACCES;
}

// Winsock 必须显式初始化，且要成对清理（RAII，AGENTS.md §5）
class SocketRuntime {
 public:
  SocketRuntime() : ok_(::WSAStartup(MAKEWORD(2, 2), &data_) == 0) {}
  ~SocketRuntime() {
    if (ok_) {
      ::WSACleanup();
    }
  }
  SocketRuntime(const SocketRuntime&) = delete;
  SocketRuntime& operator=(const SocketRuntime&) = delete;
  bool ok() const { return ok_; }

 private:
  WSADATA data_{};
  bool ok_;
};
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

void CloseSocket(SocketHandle socket) {
  ::close(socket);
}

int LastSocketError() {
  return errno;
}

bool IsInUseError(int error) {
  return error == EADDRINUSE;
}
bool IsForbiddenError(int error) {
  return error == EACCES;
}

// Linux 上不需要初始化步骤；保留同名类型让上层代码只有一份
class SocketRuntime {
 public:
  bool ok() const { return true; }
};
#endif

PortStatus TryBind(std::uint32_t address, std::uint16_t port) {
  const SocketHandle probe = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (probe == kInvalidSocket) {
    return PortStatus::kUnknown;  // 探测本身失败：不阻止启动，交给真正的监听去报错
  }
  sockaddr_in bind_address{};
  bind_address.sin_family = AF_INET;
  bind_address.sin_port = htons(port);
  bind_address.sin_addr.s_addr = htonl(address);
  // 绝不设置 SO_REUSEADDR：Windows 上它会允许抢占别人正在监听的端口，
  // 那样这个检查永远是「可用」，完全失去意义。
  const int bound = ::bind(probe, reinterpret_cast<sockaddr*>(&bind_address), sizeof(bind_address));
  if (bound == 0) {
    CloseSocket(probe);
    return PortStatus::kAvailable;
  }
  const int error = LastSocketError();
  CloseSocket(probe);
  if (IsInUseError(error)) {
    return PortStatus::kInUse;
  }
  if (IsForbiddenError(error)) {
    return PortStatus::kForbidden;
  }
  return PortStatus::kUnknown;
}

}  // namespace

PortStatus CheckListenPort(std::uint16_t port) {
  if (port == 0) {
    return PortStatus::kAvailable;  // 0 表示由系统挑一个空闲端口，永远可用
  }

  SocketRuntime runtime;
  if (!runtime.ok()) {
    return PortStatus::kUnknown;
  }

  // 两处都要探：
  //   1) 服务实际监听的是 0.0.0.0（所有网卡）；
  //   2) 但 Windows 允许在别人已绑定具体地址（如 127.0.0.1:port）时**仍然**绑通通配地址，
  //      于是「通配能绑」并不代表端口没被占——此时环回流量会被别人的套接字抢走。
  //      Linux 上第 1 步就会失败，多探一次只是保持语义一致。
  const PortStatus wildcard = TryBind(INADDR_ANY, port);
  if (wildcard != PortStatus::kAvailable) {
    return wildcard;
  }
  return TryBind(INADDR_LOOPBACK, port);
}

bool IsListenPortAvailable(std::uint16_t port) {
  const PortStatus status = CheckListenPort(port);
  return status == PortStatus::kAvailable || status == PortStatus::kUnknown;
}

}  // namespace lan_file_hub
