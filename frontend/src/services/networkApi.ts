/**
 * 系统信息：**当前**该用哪个局域网地址（AGENTS.md §14.4 的延伸）。
 *
 * 用户 2026-10-02 反馈：开机之后**切换网络**（校园网 ↔ 手机热点）时地址不会跟着变，
 * 难道每次都要重启？——服务端的 `/system/network` 是**每次请求现算**的，
 * 所以前端只要在需要时拉一次，就能拿到此刻正确的地址，不用重启任何东西。
 *
 * 拉取时机：打开 App 时 / 每 60 秒 / 窗口重新获得焦点时 / **生成分享链接之前**。
 * 结果是模块级的同步缓存（`publicUrl.ts` 里的 `serverLanAddress`），
 * 好让 `buildShareUrl` 这种纯函数直接读，不必到处传参。
 */
import type { ApiEnvelope } from '../types/auth';
import { http } from './api';
import { setServerLanAddress } from './publicUrl';

interface ServerNetworkPayload {
  lan_address: string;
  detected: boolean;
  request_host: string;
}

/** 拉一次当前局域网地址并写入缓存；失败时保留上一次的值（不打断用户操作） */
export async function refreshServerLanAddress(): Promise<string> {
  try {
    const response = await http.get<ApiEnvelope<ServerNetworkPayload>>('/system/network');
    const address = response.data.data.lan_address ?? '';
    setServerLanAddress(address);
    return address;
  } catch {
    return '';
  }
}

let started = false;

/** 打开 App 后调用一次：立刻拉一次，之后定时 + 窗口获得焦点时刷新 */
export function startServerLanRefresh(intervalMs = 60_000): void {
  void refreshServerLanAddress();
  if (started) return;
  started = true;
  window.setInterval(() => {
    void refreshServerLanAddress();
  }, intervalMs);
  window.addEventListener('focus', () => {
    void refreshServerLanAddress();
  });
  // 从 WiFi 切到热点（或反过来）时浏览器会发这个事件，正好是最需要刷新的时刻
  window.addEventListener('online', () => {
    void refreshServerLanAddress();
  });
}
