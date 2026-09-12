/** 管理员接口的类型（阶段十三）。后端响应不含密码哈希与任何凭据。 */

export type UserStatusValue = 'ACTIVE' | 'DISABLED';
export type UserRoleValue = 'ADMIN' | 'USER';

export interface AdminUser {
  id: string;
  username: string;
  email: string;
  role: UserRoleValue;
  status: UserStatusValue;
  storage_quota_bytes: number;
  used_storage_bytes: number;
  created_at: string;
  updated_at: string;
}

export interface AdminUserList {
  items: AdminUser[];
  total: number;
  page: number;
  page_size: number;
}

export interface SystemStorageStats {
  user_count: number;
  file_count: number;
  version_count: number;
  used_bytes: number;
  free_bytes: number;
  active_uploads: number;
}

export interface AuditLogEntry {
  id: string;
  actor_id: string;
  action: string;
  resource_type: string;
  resource_id: string;
  detail: Record<string, unknown>;
  created_at: string;
}

export interface AuditLogList {
  items: AuditLogEntry[];
  total: number;
  page: number;
  page_size: number;
}

export interface AuditLogQuery {
  page?: number;
  page_size?: number;
  action?: string;
  actor_id?: string;
  resource_type?: string;
  resource_id?: string;
}
