/**
 * Token 保存策略（2026-10-03 起按用户要求调整，务必与后端 docs/api.md 一致）：
 *
 *   - **密码**：只在提交表单时存在，绝不写入任何存储。
 *   - **Token**：内存 + 浏览器存储，**分两种**：
 *       1. 勾了「记住我」→ 存 `localStorage`（跨标签页、跨浏览器重启都还在），
 *          配合后端的长效令牌（`JWT_REMEMBER_EXPIRE_SECONDS`，默认 30 天）；
 *       2. 没勾 → 存 `sessionStorage`（关掉标签页/App 就没了），配合默认的短令牌。
 *
 * 为什么改（用户 2026-10-03 反馈"每次都要重新登录"）：原来一律用 sessionStorage，
 * 手机 App 的 WebView 每次冷启动都是新会话 → 每次进 App 都得重登，这是**产品缺陷**。
 * 代价必须说清楚：localStorage 是长期存储，一旦发生 XSS，Token 被窃取的窗口更长；
 * 所以「记住我」是**显式勾选**（默认勾上，方便非技术用户），并且在设置里可以取消。
 * 生产环境的正解仍是 HttpOnly + Secure + SameSite Cookie（docs/api.md §认证方案）。
 */

const TOKEN_KEY = 'lan-file-hub.access-token';
/** 记住我：令牌放在 localStorage（值为 '1'）还是 sessionStorage（不存在即否） */
const REMEMBER_KEY = 'lan-file-hub.remember-me';

let inMemoryToken: string | null = null;
/** 内存里也记住"这次是不是记住我"，免得每读一次都要碰 localStorage */
let inMemoryRemember = false;

function safeStorage(kind: 'local' | 'session'): Storage | null {
  try {
    // 隐私模式或禁用存储时访问会抛异常
    return kind === 'local' ? window.localStorage : window.sessionStorage;
  } catch {
    return null;
  }
}

/** 该从哪里读令牌：记住我就读 localStorage，否则读 sessionStorage */
function rememberRequested(): boolean {
  if (inMemoryRemember) return true;
  return safeStorage('local')?.getItem(REMEMBER_KEY) === '1';
}

export const tokenStore = {
  get(): string | null {
    if (inMemoryToken !== null) return inMemoryToken;
    const fromLocal = safeStorage('local')?.getItem(TOKEN_KEY) ?? null;
    if (fromLocal) {
      inMemoryToken = fromLocal;
      inMemoryRemember = true;
      return inMemoryToken;
    }
    inMemoryToken = safeStorage('session')?.getItem(TOKEN_KEY) ?? null;
    return inMemoryToken;
  },

  /** 保存令牌。remember = true（默认）时写 localStorage，跨重启保留 */
  set(token: string, remember = true): void {
    inMemoryToken = token;
    inMemoryRemember = remember;
    if (remember) {
      safeStorage('local')?.setItem(TOKEN_KEY, token);
      safeStorage('local')?.setItem(REMEMBER_KEY, '1');
      // 从"不记住"改成"记住"时，把会话级那份清掉，避免两处不一致
      safeStorage('session')?.removeItem(TOKEN_KEY);
      return;
    }
    safeStorage('session')?.setItem(TOKEN_KEY, token);
    safeStorage('local')?.removeItem(TOKEN_KEY);
    safeStorage('local')?.removeItem(REMEMBER_KEY);
  },

  clear(): void {
    inMemoryToken = null;
    inMemoryRemember = false;
    safeStorage('session')?.removeItem(TOKEN_KEY);
    safeStorage('local')?.removeItem(TOKEN_KEY);
    safeStorage('local')?.removeItem(REMEMBER_KEY);
  },

  /** 这台设备是否"记住了"（设置页用它显示当前状态） */
  isRemembered(): boolean {
    return rememberRequested();
  },
};
