import { afterEach, describe, expect, it, vi } from 'vitest';

import { buildInviteUrl, buildShareUrl, setServerLanAddress } from '../src/services/publicUrl';

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
  setServerLanAddress('');
});

describe('buildShareUrl（服务端上报的当前局域网地址优先）', () => {
  it('服务端上报了地址：用它拼链接，即使注入值是旧网络的地址', () => {
    // 场景：开机时是校园网 192.168.43.240，后来换成手机热点 10.200.18.61
    vi.stubEnv('VITE_LFH_LAN_URL', 'http://192.168.43.240:5180');
    setServerLanAddress('10.200.18.61');

    const result = buildShareUrl('tok', { origin: 'http://127.0.0.1:5180', hostname: '127.0.0.1' });

    expect(result.url).toBe('http://10.200.18.61:5180/s/tok');
    expect(result.warning).toContain('10.200.18.61');
  });

  it('端口取当前页面自己的（便携包 9180 / 开发 5173 都对）', () => {
    setServerLanAddress('192.168.1.9');

    expect(buildShareUrl('t', { origin: 'http://127.0.0.1:9180', hostname: '127.0.0.1' }).url).toBe(
      'http://192.168.1.9:9180/s/t',
    );
    expect(buildShareUrl('t', { origin: 'http://localhost:5173', hostname: 'localhost' }).url).toBe(
      'http://192.168.1.9:5173/s/t',
    );
  });

  it('服务端没探到地址（空串）时：退回启动脚本注入的地址', () => {
    vi.stubEnv('VITE_LFH_LAN_URL', 'http://192.168.43.240:5173');
    setServerLanAddress('');

    expect(buildShareUrl('t', { origin: 'http://127.0.0.1:5173', hostname: '127.0.0.1' }).url).toBe(
      'http://192.168.43.240:5173/s/t',
    );
  });
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

describe('buildInviteUrl（邀请链接，与分享链接同一套规则）', () => {
  it('本机地址打开时：换成服务端上报的局域网地址，路径是 /register?invite=', () => {
    setServerLanAddress('10.200.18.61');

    const result = buildInviteUrl('tok-abc', {
      origin: 'http://127.0.0.1:9180',
      hostname: '127.0.0.1',
    });

    expect(result.url).toBe('http://10.200.18.61:9180/register?invite=tok-abc');
    expect(result.warning).toBeTruthy();
  });

  it('token 会被 URL 编码（不会拼出坏链接）', () => {
    setServerLanAddress('192.168.1.9');

    expect(
      buildInviteUrl('a b&c', { origin: 'http://127.0.0.1:9180', hostname: '127.0.0.1' }).url,
    ).toBe('http://192.168.1.9:9180/register?invite=a%20b%26c');
  });

  it('本来就是局域网地址访问时：直接用当前地址，不给提示', () => {
    const result = buildInviteUrl('t', {
      origin: 'http://192.168.1.5:9180',
      hostname: '192.168.1.5',
    });

    expect(result.url).toBe('http://192.168.1.5:9180/register?invite=t');
    expect(result.warning).toBeUndefined();
  });
});
