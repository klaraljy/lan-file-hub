/**
 * 在线状态辅助函数的单元测试（五阶段改造 ⑤：广场）。
 *
 * 这里只测纯逻辑（设备名识别、device_id 生成与复用）——心跳的定时行为由 e2e 覆盖。
 */
import { describe, expect, it } from 'vitest';

import {
  HEARTBEAT_INTERVAL_MS,
  browserNameFor,
  deviceNameFor,
  ensureDeviceId,
  osNameFor,
} from '../src/features/presence/usePresence';

const CHROME_WINDOWS =
  'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36';
const SAFARI_IPHONE =
  'Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1';
const ANDROID_CHROME =
  'Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36';

describe('设备名识别', () => {
  it('认得出常见浏览器', () => {
    expect(browserNameFor(CHROME_WINDOWS)).toBe('Chrome');
    expect(browserNameFor(SAFARI_IPHONE)).toBe('Safari');
    expect(browserNameFor('Mozilla/5.0 Firefox/133.0')).toBe('Firefox');
    expect(browserNameFor('Mozilla/5.0 Edg/131.0')).toBe('Edge');
  });

  it('认得出常见系统', () => {
    expect(osNameFor(CHROME_WINDOWS)).toBe('Windows');
    expect(osNameFor(SAFARI_IPHONE)).toBe('iOS');
    expect(osNameFor(ANDROID_CHROME)).toBe('Android');
  });

  it('拼出来的名字是"浏览器 · 系统"', () => {
    expect(deviceNameFor(CHROME_WINDOWS)).toBe('Chrome · Windows');
    expect(deviceNameFor(ANDROID_CHROME)).toBe('Chrome · Android');
  });

  it('认不出来也别报错，给出可读兜底', () => {
    expect(deviceNameFor('something-weird')).toBe('浏览器 · 未知系统');
  });
});

describe('本机 device_id', () => {
  it('第一次生成、第二次复用同一个', () => {
    const storage = new Map<string, string>();
    const fake: Storage = {
      get length() {
        return storage.size;
      },
      clear: () => storage.clear(),
      getItem: (key: string) => storage.get(key) ?? null,
      key: (index: number) => Array.from(storage.keys())[index] ?? null,
      removeItem: (key: string) => void storage.delete(key),
      setItem: (key: string, value: string) => void storage.set(key, value),
    };

    const first = ensureDeviceId(fake);
    const second = ensureDeviceId(fake);
    expect(second).toBe(first);
    // 后端只接受 [A-Za-z0-9._-]，长度 ≤ 64
    expect(first).toMatch(/^[A-Za-z0-9._-]{1,64}$/);
  });

  it('本地存着非法值时重新生成', () => {
    const store = new Map<string, string>([['lfh.device_id', 'bad id!!']]);
    const fake = {
      get length() {
        return store.size;
      },
      clear: () => store.clear(),
      getItem: (key: string) => store.get(key) ?? null,
      key: (index: number) => Array.from(store.keys())[index] ?? null,
      removeItem: (key: string) => void store.delete(key),
      setItem: (key: string, value: string) => void store.set(key, value),
    } as Storage;

    const id = ensureDeviceId(fake);
    expect(id).not.toBe('bad id!!');
    expect(id).toMatch(/^[A-Za-z0-9._-]{1,64}$/);
  });

  it('心跳间隔比服务端 TTL 短（否则会频繁被判离线）', () => {
    expect(HEARTBEAT_INTERVAL_MS).toBeLessThan(90_000);
  });
});
