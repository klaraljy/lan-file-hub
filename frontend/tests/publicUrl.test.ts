import { afterEach, describe, expect, it, vi } from 'vitest';

import { buildShareUrl } from '../src/services/publicUrl';

/**
 * 分享链接必须"别人手机点得开"。
 * 这条规则来自真实故障（AGENTS.md §14.4）：用户在电脑上用 127.0.0.1 打开页面，
 * 生成的链接指向手机自己 → 手机上必然打不开。
 *
 * 注：jsdom 的 `window.location` 改不动（[LegacyUnforgeable]），
 * 所以这里用 buildShareUrl 的可选 location 入参来喂各种访问地址；
 * 最后一条用例专门验证"不传时走真实 window.location"这条默认路径。
 */

const LAN_URL = 'http://192.168.43.240:5173';

afterEach(() => {
  vi.unstubAllEnvs();
});

describe('buildShareUrl', () => {
  it('127.0.0.1 打开且有局域网地址时：换成局域网地址，并提示已经换过', () => {
    vi.stubEnv('VITE_LFH_LAN_URL', LAN_URL);

    const result = buildShareUrl('tok123', {
      origin: 'http://127.0.0.1:5173',
      hostname: '127.0.0.1',
    });

    expect(result.url).toBe(`${LAN_URL}/s/tok123`);
    expect(result.warning).toBeTruthy();
  });

  it('localhost 与 ::1 同样算本机地址', () => {
    vi.stubEnv('VITE_LFH_LAN_URL', LAN_URL);

    expect(
      buildShareUrl('t', { origin: 'http://localhost:5173', hostname: 'localhost' }).url,
    ).toBe(`${LAN_URL}/s/t`);
    expect(buildShareUrl('t', { origin: 'http://[::1]:5173', hostname: '[::1]' }).url).toBe(
      `${LAN_URL}/s/t`,
    );
  });

  it('注入的局域网地址结尾带斜杠时，不会拼出双斜杠', () => {
    vi.stubEnv('VITE_LFH_LAN_URL', `${LAN_URL}/`);

    expect(buildShareUrl('t', { origin: 'http://localhost:5173', hostname: 'localhost' }).url).toBe(
      `${LAN_URL}/s/t`,
    );
  });

  it('本机地址但没拿到局域网地址时：仍然给出警告，说清"别人手机打不开"', () => {
    vi.stubEnv('VITE_LFH_LAN_URL', '');

    const result = buildShareUrl('tok123', {
      origin: 'http://127.0.0.1:5173',
      hostname: '127.0.0.1',
    });

    expect(result.url).toBe('http://127.0.0.1:5173/s/tok123');
    expect(result.warning).toContain('手机');
  });

  it('本来就是局域网地址访问时：直接用当前地址，不给多余提示', () => {
    vi.stubEnv('VITE_LFH_LAN_URL', 'http://192.168.1.5:5173');

    const result = buildShareUrl('tok123', {
      origin: LAN_URL,
      hostname: '192.168.43.240',
    });

    expect(result.url).toBe(`${LAN_URL}/s/tok123`);
    expect(result.warning).toBeUndefined();
  });

  it('域名访问（生产/反向代理）时：用当前域名，不给提示', () => {
    vi.stubEnv('VITE_LFH_LAN_URL', LAN_URL);

    const result = buildShareUrl('tok123', {
      origin: 'https://files.example.com',
      hostname: 'files.example.com',
    });

    expect(result.url).toBe('https://files.example.com/s/tok123');
    expect(result.warning).toBeUndefined();
  });

  it('不传 location 时用真实的 window.location（jsdom 默认是 localhost）', () => {
    vi.stubEnv('VITE_LFH_LAN_URL', '');

    const result = buildShareUrl('tok123');

    expect(result.url.endsWith('/s/tok123')).toBe(true);
    expect(result.url.startsWith('http://localhost')).toBe(true);
    expect(result.warning).toBeTruthy();
  });
});
