import axios, { AxiosError } from 'axios';

import type { ApiEnvelope, ApiErrorBody, AuthPayload, User } from '../types/auth';
import { tokenStore } from './tokenStore';

/** 用户可理解的错误。后端返回的 message 已经是可以直接展示的中文文案。 */
export class ApiError extends Error {
  readonly code: string;
  readonly requestId: string;
  /** 后端在 error.details 里回的结构化信息（如版本冲突的 current_version_id） */
  readonly details: Record<string, string>;

  constructor(
    code: string,
    message: string,
    requestId: string,
    details: Record<string, string> = {},
  ) {
    super(message);
    this.name = 'ApiError';
    this.code = code;
    this.requestId = requestId;
    this.details = details;
  }

  /** 该错误是否值得重试（网络类问题才值得） */
  get retryable(): boolean {
    return this.code === 'NETWORK_ERROR' || this.code === 'STORAGE_UNAVAILABLE';
  }
}

// 401 时通知上层（AuthContext 注册回调），避免在拦截器里直接操作路由
let onUnauthorized: (() => void) | null = null;

export function setUnauthorizedHandler(handler: (() => void) | null): void {
  onUnauthorized = handler;
}

export const http = axios.create({
  baseURL: '/api/v1',
  timeout: 15000,
  headers: { 'Content-Type': 'application/json' },
});

http.interceptors.request.use((config) => {
  const token = tokenStore.get();
  if (token) {
    config.headers.Authorization = `Bearer ${token}`;
  }
  return config;
});

http.interceptors.response.use(
  (response) => response,
  (error: AxiosError<ApiErrorBody>) => {
    // 网络层失败（断网、超时、后端未启动）
    if (!error.response) {
      return Promise.reject(new ApiError('NETWORK_ERROR', '无法连接服务器，请检查网络后重试', ''));
    }

    const body = error.response.data;
    const code = body?.error?.code ?? 'INTERNAL_ERROR';
    const message = body?.error?.message ?? `请求失败（HTTP ${error.response.status}）`;
    const requestId = body?.request_id ?? '';
    const details = (body?.error?.details ?? {}) as Record<string, string>;

    // Token 失效或过期：清掉本地状态并让上层跳转登录
    if (error.response.status === 401) {
      tokenStore.clear();
      onUnauthorized?.();
    }

    return Promise.reject(new ApiError(code, message, requestId, details));
  },
);

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

export const authApi = {
  async register(
    username: string,
    email: string,
    password: string,
    /** 邀请制注册：管理员发的邀请 token（关闭自助注册时必填） */
    inviteToken?: string,
    /** 「记住我」（2026-10-03）：true 时后端发长效令牌，前端存 localStorage */
    remember = true,
  ): Promise<AuthPayload> {
    const response = await http.post<ApiEnvelope<AuthPayload>>('/auth/register', {
      username,
      email,
      password,
      remember,
      ...(inviteToken ? { invite_token: inviteToken } : {}),
    });
    return unwrap(response.data);
  },

  async login(identifier: string, password: string, remember = true): Promise<AuthPayload> {
    // 后端的 identifier 走 username 字段；带 @ 时按邮箱处理
    const payload = identifier.includes('@')
      ? { email: identifier, password, remember }
      : { username: identifier, password, remember };
    const response = await http.post<ApiEnvelope<AuthPayload>>('/auth/login', payload);
    return unwrap(response.data);
  },

  async logout(): Promise<void> {
    await http.post('/auth/logout');
  },

  /** 登录态恢复：用已保存的 Token 换取当前用户 */
  async me(): Promise<User> {
    const response = await http.get<ApiEnvelope<User>>('/auth/me');
    return unwrap(response.data);
  },
};
