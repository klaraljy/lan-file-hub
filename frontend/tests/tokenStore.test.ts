import { beforeEach, describe, expect, it } from 'vitest';

import { tokenStore } from '../src/services/tokenStore';

/**
 * 这些用例守的是登录态的两条要求（2026-10-03 按用户反馈调整过策略）：
 *   - 口令绝不进任何存储；
 *   - 「记住我」勾上 → 令牌进 localStorage（跨 App/浏览器重启还在，配后端 30 天长效令牌）；
 *     没勾 → 只进 sessionStorage（关掉标签页就没了，配默认短令牌）。
 *
 * 背景：用户 2026-10-03 反馈"每次打开 App 都要重新登录"。根因就是原来一律用
 * sessionStorage + 后端 1 小时短令牌 —— 这是产品缺陷，所以策略改成上面这样。
 */
describe('tokenStore', () => {
  beforeEach(() => {
    tokenStore.clear();
    window.sessionStorage.clear();
    window.localStorage.clear();
  });

  it('未登录时返回 null', () => {
    expect(tokenStore.get()).toBeNull();
  });

  it('保存后可以读回', () => {
    tokenStore.set('token-abc');
    expect(tokenStore.get()).toBe('token-abc');
  });

  it('记住我（默认）：令牌进 localStorage，不留在 sessionStorage', () => {
    tokenStore.set('token-abc', true);

    expect(Object.values(window.localStorage)).toContain('token-abc');
    expect(window.sessionStorage.length).toBe(0);
    expect(tokenStore.isRemembered()).toBe(true);
  });

  it('不记住：令牌只进 sessionStorage，localStorage 必须干净', () => {
    tokenStore.set('token-abc', false);

    const sessionValues = Object.values(window.sessionStorage);
    expect(sessionValues).toContain('token-abc');
    expect(window.localStorage.length).toBe(0);
    expect(tokenStore.isRemembered()).toBe(false);
  });

  it('从"不记住"改成"记住"时不会两处都留着', () => {
    tokenStore.set('token-old', false);
    tokenStore.set('token-new', true);

    expect(window.sessionStorage.length).toBe(0);
    expect(tokenStore.get()).toBe('token-new');
  });

  it('清除后两边都不留下痕迹（退出登录必须清干净）', () => {
    tokenStore.set('token-abc', true);
    tokenStore.clear();

    expect(tokenStore.get()).toBeNull();
    expect(window.sessionStorage.length).toBe(0);
    expect(window.localStorage.length).toBe(0);
    expect(tokenStore.isRemembered()).toBe(false);
  });

  it('不会把密码写进任何存储', () => {
    // 模拟一次登录流程里可能出现的敏感值
    tokenStore.set('token-abc', true);
    const password = 'correct-horse-battery';

    const dump = JSON.stringify({
      session: Object.entries(window.sessionStorage),
      local: Object.entries(window.localStorage),
    });
    expect(dump).not.toContain(password);
  });
});
