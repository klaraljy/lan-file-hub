/** 与后端 docs/api.md 对应的类型定义。 */

export type UserRole = 'USER' | 'ADMIN';
export type UserStatus = 'ACTIVE' | 'DISABLED';

export interface User {
  id: string;
  username: string;
  email: string;
  role: UserRole;
  status: UserStatus;
  storage_quota_bytes: number;
  used_storage_bytes: number;
  created_at: string;
  updated_at: string;
}

export interface AuthPayload {
  access_token: string;
  token_type: string;
  expires_in: number;
  user: User;
}

/** 后端统一响应信封：{ data, request_id } */
export interface ApiEnvelope<T> {
  data: T;
  request_id: string;
}

/** 后端统一错误信封：{ error: { code, message, details }, request_id } */
export interface ApiErrorBody {
  error: {
    code: string;
    message: string;
    details?: Record<string, unknown>;
  };
  request_id: string;
}
