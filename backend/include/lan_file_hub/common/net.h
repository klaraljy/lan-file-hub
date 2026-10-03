// 网络自检工具（启动前端口检查，阶段十六补丁）。
//
// 为什么需要它：Drogon 在监听端口绑定失败时只打一行 FATAL 日志，进程**仍然留在事件循环里**
// 不退出——外部看到的是「进程活着、端口没人听」，容器编排会以为服务已启动，一键启动脚本
// 也会误判成「后端已在运行」。所以服务启动前自己先探一次端口，不可用就明确报错并非零退出。
#ifndef LAN_FILE_HUB_COMMON_NET_H
#define LAN_FILE_HUB_COMMON_NET_H

#include <cstdint>
#include <string>

namespace lan_file_hub {

// 端口可用性的三种结果。**必须区分「被占用」与「被系统保留」**：
// Windows 上保留端口段（Docker Desktop / Hyper-V / WSL 会预留，例如本机实测的 7920-8419）
// 会让 bind 直接失败并返回 WSAEACCES，此时**没有任何进程在监听这个端口**，
// 若只报「端口已被占用」，排查方向会被彻底带偏（实测踩到过：8080 落在保留段里，
// 报"被占用"却查不到占用者）。
enum class PortStatus {
  kAvailable,  // 可以绑定
  kInUse,      // 已被占用（EADDRINUSE / WSAEADDRINUSE）
  kForbidden,  // 被系统保留或权限不足（EACCES / WSAEACCES）
  kUnknown,    // 探测环境异常（例如套接字创建失败）——不阻止启动
};

// 探测「在本机所有网卡上监听 port」是否可用。
// 先探 0.0.0.0，再探 127.0.0.1：Windows 允许在别人已绑定具体地址（如 127.0.0.1）时
// **仍然绑通通配地址**，只探通配会漏判。
PortStatus CheckListenPort(std::uint16_t port);

// 只有 kAvailable 与 kUnknown 视为「可以启动」。
bool IsListenPortAvailable(std::uint16_t port);

// 本机在局域网里的地址 —— 手机 / 其它电脑该用的那个（分享链接、启动面板都要用）。
//
// 做法：建一个 UDP 套接字并 `connect` 到一个外部地址（**UDP connect 不发任何数据包**，
// 只是让内核按当前路由表选出出口网卡），再 `getsockname()` 读出本机地址。
// 好处：跨平台、不必枚举网卡，拿到的正是「当前有默认路由的那块网卡」——
// 用户换 WiFi / 换手机热点之后，**下一次调用就是新地址**，服务不用重启
// （用户 2026-10-02 反馈：换网络后地址不变，难道每次都要重启）。
//
// 没有默认路由时（例如只接了交换机、没有网关）返回空串，调用方自行回退。
[[nodiscard]] std::string GetPrimaryLocalAddress();

}  // namespace lan_file_hub

#endif  // LAN_FILE_HUB_COMMON_NET_H
