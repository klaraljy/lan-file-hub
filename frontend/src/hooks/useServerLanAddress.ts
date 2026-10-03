/**
 * 订阅"当前该用哪个局域网地址"（2026-10-03）。
 *
 * 地址由 `services/networkApi.ts` 定时 / 窗口聚焦 / 网络恢复时刷新（换 WiFi 会变），
 * 这里把它接进 React 状态 —— 「手机怎么连」卡片上的地址与二维码因此会自己更新。
 */
import { useEffect, useState } from 'react';

import { getServerLanAddress, subscribeServerLanAddress } from '../services/publicUrl';

export function useServerLanAddress(): string {
  const [address, setAddress] = useState(getServerLanAddress);

  useEffect(() => subscribeServerLanAddress(setAddress), []);

  return address;
}
