/**
 * 在线状态心跳（五阶段改造 ⑤：广场，AGENTS.md §15）。
 *
 * 设计要点：
 *   - 本机 device_id **随机生成一次并存在浏览器本地**，之后不再变（后端只允许 [A-Za-z0-9._-]）；
 *   - 每 30 秒心跳一次；标签页在后台时不心跳（省电），回到前台**立刻补一次**；
 *   - 服务端 TTL 90 秒：关掉页面后最多 90 秒别人就看不到你了；
 *   - 心跳结果放在 react-query 缓存里，广场页与设置页共用同一份数据。
 */
import { useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';

import { presenceApi, type HeartbeatResult } from '../../services/presenceApi';
import { useAuth } from '../auth/AuthContext';

const DEVICE_ID_KEY = 'lfh.device_id';

/** 心跳间隔：服务端 TTL 是 90 秒，30 秒一次留了两次容错 */
export const HEARTBEAT_INTERVAL_MS = 30_000;

/** 浏览器名（纯函数，方便单测） */
export function browserNameFor(userAgent: string): string {
  if (/Edg\//.test(userAgent)) return 'Edge';
  if (/OPR\//.test(userAgent)) return 'Opera';
  if (/Firefox\//.test(userAgent)) return 'Firefox';
  if (/Chrome\//.test(userAgent)) return 'Chrome';
  if (/Safari\//.test(userAgent)) return 'Safari';
  return '浏览器';
}

/** 系统名（纯函数，方便单测） */
export function osNameFor(userAgent: string): string {
  if (/Android/i.test(userAgent)) return 'Android';
  if (/iPhone|iPad|iPod/i.test(userAgent)) return 'iOS';
  if (/Windows/i.test(userAgent)) return 'Windows';
  if (/Mac OS X/i.test(userAgent)) return 'macOS';
  if (/Linux/i.test(userAgent)) return 'Linux';
  return '未知系统';
}

/** 展示用设备名，例如 "Chrome · Windows" */
export function deviceNameFor(userAgent: string): string {
  return `${browserNameFor(userAgent)} · ${osNameFor(userAgent)}`;
}

function randomSuffix(): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return crypto.randomUUID().replace(/[^A-Za-z0-9]/g, '');
  }
  return Math.random().toString(36).slice(2, 12);
}

/**
 * 本机设备标识：生成一次后长期复用。
 * 只包含后端允许的字符，长度也控制在 64 以内（超了或非法就重新生成）。
 */
export function ensureDeviceId(storage: Storage = window.localStorage): string {
  const existing = storage.getItem(DEVICE_ID_KEY);
  if (existing && /^[A-Za-z0-9._-]{1,64}$/.test(existing)) {
    return existing;
  }
  const id = `web-${randomSuffix().slice(0, 24)}`.slice(0, 64);
  storage.setItem(DEVICE_ID_KEY, id);
  return id;
}

export const presenceKeys = {
  heartbeat: ['presence', 'heartbeat'] as const,
};

/**
 * 心跳（登录后自动开始，登出后停止）。
 *
 * 用法：在应用外壳里调用一次即可（广场页与设置页读同一份缓存）。
 */
export function usePresenceHeartbeat() {
  const { status } = useAuth();
  const enabled = status === 'authenticated';

  const query = useQuery<HeartbeatResult>({
    queryKey: presenceKeys.heartbeat,
    queryFn: () =>
      presenceApi.heartbeat({
        device_id: ensureDeviceId(),
        device_name: deviceNameFor(navigator.userAgent),
        platform: 'web',
      }),
    enabled,
    refetchInterval: HEARTBEAT_INTERVAL_MS,
    // 后台标签页不心跳：省电，且 90 秒后服务端自然判离线
    refetchIntervalInBackground: false,
    staleTime: HEARTBEAT_INTERVAL_MS,
    // 心跳失败不重试：30 秒后本来就会再来一次，重试只会放大失败
    retry: false,
  });

  const refetch = query.refetch;
  useEffect(() => {
    if (!enabled) {
      return;
    }
    // 从后台切回前台时立刻补一次，避免"刚回到页面时别人看我已离线"
    function onVisibilityChange() {
      if (document.visibilityState === 'visible') {
        void refetch();
      }
    }
    document.addEventListener('visibilitychange', onVisibilityChange);
    return () => document.removeEventListener('visibilitychange', onVisibilityChange);
  }, [enabled, refetch]);

  return query;
}
