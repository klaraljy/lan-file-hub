/** 管理员接口（阶段十三第 13～17 条）。仅管理员可用，后端会再次校验权限。 */
import type { ApiEnvelope } from '../types/auth';
import type {
  AdminUserList,
  AuditLogList,
  AuditLogQuery,
  SystemStorageStats,
  UserStatusValue,
} from '../types/admin';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

export interface ListUsersParams {
  page?: number;
  page_size?: number;
  search?: string;
  status?: UserStatusValue;
  /** 白名单字段；后端会拒绝未知字段（不会静默回退） */
  sort_by?: 'name' | 'username' | 'email' | 'created_at' | 'used_storage_bytes' | 'storage_quota_bytes';
  sort_order?: 'asc' | 'desc';
}

export const adminApi = {
  async listUsers(params: ListUsersParams = {}): Promise<AdminUserList> {
    const response = await http.get<ApiEnvelope<AdminUserList>>('/admin/users', { params });
    return unwrap(response.data);
  },

  async setUserStatus(userId: string, status: UserStatusValue): Promise<void> {
    await http.patch(`/admin/users/${userId}/status`, { status });
  },

  async setUserQuota(userId: string, storageQuotaBytes: number): Promise<void> {
    await http.patch(`/admin/users/${userId}/quota`, {
      storage_quota_bytes: storageQuotaBytes,
    });
  },

  async storageStats(): Promise<SystemStorageStats> {
    const response = await http.get<ApiEnvelope<SystemStorageStats>>('/admin/storage');
    return unwrap(response.data);
  },

  async listAuditLogs(params: AuditLogQuery = {}): Promise<AuditLogList> {
    const response = await http.get<ApiEnvelope<AuditLogList>>('/admin/audit-logs', { params });
    return unwrap(response.data);
  },
};
