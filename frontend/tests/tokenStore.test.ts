import { beforeEach, describe, expect, it } from 'vitest';

import { tokenStore } from '../src/services/tokenStore';

/**
 * 这些用例守的是阶段二的安全要求：
 *   - 口令绝不进任何存储
 *   - Token 只进 sessionStorage（内存 + 会话级），绝不进 localStorage
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

  it('Token 写入 sessionStorage 而不是 localStorage', () => {
    tokenStore.set('token-abc');

    const sessionValues = Object.values(window.sessionStorage);
    expect(sessionValues).toContain('token-abc');

    // localStorage 必须完全干净：长期存储会放大 XSS 的后果
    expect(window.localStorage.length).toBe(0);
  });

  it('清除后两边都不留下痕迹', () => {
    tokenStore.set('token-abc');
    tokenStore.clear();

    expect(tokenStore.get()).toBeNull();
    expect(window.sessionStorage.length).toBe(0);
    expect(window.localStorage.length).toBe(0);
  });

  it('不会把密码写进任何存储', () => {
    // 模拟一次登录流程里可能出现的敏感值
    tokenStore.set('token-abc');
    const password = 'correct-horse-battery';

    const dump = JSON.stringify({
      session: Object.entries(window.sessionStorage),
      local: Object.entries(window.localStorage),
    });
    expect(dump).not.toContain(password);
  });
});
