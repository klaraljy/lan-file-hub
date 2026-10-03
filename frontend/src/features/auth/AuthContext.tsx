import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';

import { ApiError, authApi, setUnauthorizedHandler } from '../../services/api';
import { tokenStore } from '../../services/tokenStore';
import type { User } from '../../types/auth';

export type AuthStatus = 'restoring' | 'authenticated' | 'anonymous';

interface AuthContextValue {
  status: AuthStatus;
  user: User | null;
  login: (identifier: string, password: string) => Promise<void>;
  register: (
    username: string,
    email: string,
    password: string,
    /** 邀请制注册：邀请链接里的 token（关闭自助注册时必填） */
    inviteToken?: string,
  ) => Promise<void>;
  logout: () => Promise<void>;
}

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [status, setStatus] = useState<AuthStatus>('restoring');
  const [user, setUser] = useState<User | null>(null);

  const forgetSession = useCallback(() => {
    tokenStore.clear();
    setUser(null);
    setStatus('anonymous');
  }, []);

  // 401 统一处理：任何请求发现 Token 失效都会走到这里
  useEffect(() => {
    setUnauthorizedHandler(forgetSession);
    return () => setUnauthorizedHandler(null);
  }, [forgetSession]);

  // 登录态恢复：页面刷新后凭 sessionStorage 里的 Token 重新拉一次 /auth/me
  useEffect(() => {
    let cancelled = false;

    async function restore() {
      if (!tokenStore.get()) {
        if (!cancelled) setStatus('anonymous');
        return;
      }
      try {
        const current = await authApi.me();
        if (cancelled) return;
        setUser(current);
        setStatus('authenticated');
      } catch (error) {
        if (cancelled) return;
        // Token 过期或被禁用：静默回到未登录，不弹错误（用户没做错什么）
        if (error instanceof ApiError) {
          tokenStore.clear();
        }
        setUser(null);
        setStatus('anonymous');
      }
    }

    void restore();
    return () => {
      cancelled = true;
    };
  }, []);

  const login = useCallback(async (identifier: string, password: string) => {
    const payload = await authApi.login(identifier, password);
    tokenStore.set(payload.access_token);
    setUser(payload.user);
    setStatus('authenticated');
  }, []);

  const register = useCallback(
    async (username: string, email: string, password: string, inviteToken?: string) => {
      const payload = await authApi.register(username, email, password, inviteToken);
      tokenStore.set(payload.access_token);
      setUser(payload.user);
      setStatus('authenticated');
    },
    [],
  );

  const logout = useCallback(async () => {
    try {
      await authApi.logout();
    } catch {
      // 服务端无状态，登出失败也要把本地状态清干净
    } finally {
      forgetSession();
    }
  }, [forgetSession]);

  const value = useMemo(
    () => ({ status, user, login, register, logout }),
    [status, user, login, register, logout],
  );

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthContextValue {
  const context = useContext(AuthContext);
  if (!context) {
    throw new Error('useAuth 必须在 AuthProvider 内部使用');
  }
  return context;
}
