/**
 * Token 保存策略（阶段二要求，务必与后端 docs/api.md 保持一致）：
 *
 *   - **密码**：只在提交表单时存在，绝不写入任何存储。
 *   - **Token**：内存 + sessionStorage。选择 sessionStorage 而不是 localStorage 的原因：
 *       1. localStorage 是长期存储，XSS 一旦发生，Token 会被长期窃取；
 *       2. sessionStorage 随标签页关闭而清除，暴露窗口更小。
 *     两者都能被 XSS 读取，所以**生产环境应改为 HttpOnly + Secure + SameSite Cookie**
 *     （docs/api.md §认证方案），这需要后端配合，属于后续阶段的工作。
 *   - 这里明确**不使用 localStorage**，并且只存 Token，不存用户资料。
 */

const TOKEN_KEY = 'lan-file-hub.access-token';

let inMemoryToken: string | null = null;

function safeSessionStorage(): Storage | null {
  try {
    // 隐私模式或禁用存储时访问会抛异常
    return window.sessionStorage;
  } catch {
    return null;
  }
}

export const tokenStore = {
  get(): string | null {
    if (inMemoryToken !== null) return inMemoryToken;
    const storage = safeSessionStorage();
    if (!storage) return null;
    inMemoryToken = storage.getItem(TOKEN_KEY);
    return inMemoryToken;
  },

  set(token: string): void {
    inMemoryToken = token;
    safeSessionStorage()?.setItem(TOKEN_KEY, token);
  },

  clear(): void {
    inMemoryToken = null;
    safeSessionStorage()?.removeItem(TOKEN_KEY);
  },
};
