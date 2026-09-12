// 端口自检工具的单元测试。
// 用途：服务启动前发现「端口被占用」要立刻报错退出，而不是变成不监听端口的僵尸进程。
#include <gtest/gtest.h>

#include <cstdint>

#include "lan_file_hub/common/net.h"

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
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

void CloseSocket(SocketHandle socket) {
  ::close(socket);
}
#endif

class NetTest : public ::testing::Test {
 protected:
  void SetUp() override {
#if defined(_WIN32)
    ASSERT_EQ(::WSAStartup(MAKEWORD(2, 2), &wsa_data_), 0);
#endif
  }
  void TearDown() override {
#if defined(_WIN32)
    ::WSACleanup();
#endif
  }

  // 在指定地址上监听一个系统分配的端口，返回端口号（0 表示失败）。
  // 监听套接字通过 out_socket 交回调用方，由调用方负责关闭。
  static std::uint16_t ListenOnEphemeralPort(SocketHandle& out_socket, std::uint32_t address) {
    out_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (out_socket == kInvalidSocket) {
      return 0;
    }
    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(0);
    bind_address.sin_addr.s_addr = htonl(address);
    if (::bind(out_socket, reinterpret_cast<sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
      CloseSocket(out_socket);
      out_socket = kInvalidSocket;
      return 0;
    }
    if (::listen(out_socket, 1) != 0) {
      CloseSocket(out_socket);
      out_socket = kInvalidSocket;
      return 0;
    }
    sockaddr_in bound{};
#if defined(_WIN32)
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    if (::getsockname(out_socket, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
      CloseSocket(out_socket);
      out_socket = kInvalidSocket;
      return 0;
    }
    return ntohs(bound.sin_port);
  }

#if defined(_WIN32)
  WSADATA wsa_data_{};
#endif
};

// 服务自己也监听 0.0.0.0，所以这是最常见的冲突：另一个实例占着同一个端口
TEST_F(NetTest, WildcardListenerMakesPortUnavailable) {
  SocketHandle listener = kInvalidSocket;
  const std::uint16_t port = ListenOnEphemeralPort(listener, INADDR_ANY);
  ASSERT_NE(port, 0U) << "无法创建监听套接字";
  EXPECT_FALSE(IsListenPortAvailable(port))
      << "端口 " << port << " 正被 0.0.0.0 监听，却判定为可用";
  CloseSocket(listener);
}

// Windows 允许在别人已绑 127.0.0.1:port 时仍然绑通 0.0.0.0:port，
// 所以「通配地址能绑」不代表端口没被占——只探通配地址会漏判（实测踩到过）。
TEST_F(NetTest, LoopbackListenerMakesPortUnavailable) {
  SocketHandle listener = kInvalidSocket;
  const std::uint16_t port = ListenOnEphemeralPort(listener, INADDR_LOOPBACK);
  ASSERT_NE(port, 0U) << "无法创建监听套接字";
  EXPECT_FALSE(IsListenPortAvailable(port))
      << "端口 " << port << " 正被 127.0.0.1 监听，却判定为可用";
  CloseSocket(listener);
}

TEST_F(NetTest, FreePortIsReportedAvailable) {
  bool available = false;
  for (int attempt = 0; attempt < 5 && !available; ++attempt) {
    SocketHandle listener = kInvalidSocket;
    const std::uint16_t port = ListenOnEphemeralPort(listener, INADDR_ANY);
    ASSERT_NE(port, 0U) << "无法创建监听套接字";
    CloseSocket(listener);
    // 刚释放的端口理论上立即可用；但别的进程可能刚好抢走它，
    // 所以允许换一个临时端口重试，避免用例偶发失败。
    available = IsListenPortAvailable(port);
  }
  EXPECT_TRUE(available) << "连续 5 个刚释放的临时端口都判定为被占用，探测逻辑可疑";
}

TEST_F(NetTest, PortZeroIsAlwaysAvailable) {
  EXPECT_TRUE(IsListenPortAvailable(0)) << "端口 0 表示由系统分配，不应判定为被占用";
}

// 只报「可用/不可用」不够：被占用与被系统保留的处理方式完全不同，
// 所以状态必须能区分出来（Windows 保留端口段返回 WSAEACCES，此时没有任何监听者）。
TEST_F(NetTest, StatusReportsInUseForOccupiedPort) {
  SocketHandle listener = kInvalidSocket;
  const std::uint16_t port = ListenOnEphemeralPort(listener, INADDR_ANY);
  ASSERT_NE(port, 0U) << "无法创建监听套接字";
  EXPECT_EQ(static_cast<int>(CheckListenPort(port)), static_cast<int>(PortStatus::kInUse));
  CloseSocket(listener);
}

TEST_F(NetTest, StatusReportsAvailableForFreePort) {
  bool available = false;
  for (int attempt = 0; attempt < 5 && !available; ++attempt) {
    SocketHandle listener = kInvalidSocket;
    const std::uint16_t port = ListenOnEphemeralPort(listener, INADDR_ANY);
    ASSERT_NE(port, 0U) << "无法创建监听套接字";
    CloseSocket(listener);
    available = CheckListenPort(port) == PortStatus::kAvailable;
  }
  EXPECT_TRUE(available) << "连续 5 个刚释放的临时端口都不是 kAvailable，探测逻辑可疑";
}

}  // namespace
}  // namespace lan_file_hub
