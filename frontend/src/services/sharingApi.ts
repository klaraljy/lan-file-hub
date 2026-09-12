/** 权限与分享接口（阶段九）。 */
import type { ApiEnvelope } from '../types/auth';
import type {
  EffectiveLevel,
  PermissionGrant,
  PermissionValue,
  PublicShareView,
  ShareLink,
  ShareListing,
  SharePermission,
  SharedWithMeListing,
} from '../types/share';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

export interface CreateShareInput {
  permission: SharePermission;
  expiresInSeconds?: number;
  maxDownloads?: number;
}

export const sharingApi = {
  /** 当前用户对该资源的有效权限（前端据此显示/隐藏按钮；后端仍然独立校验） */
  async effectiveLevel(resourceId: string): Promise<EffectiveLevel> {
    const response = await http.get<ApiEnvelope<{ level: EffectiveLevel }>>(
      `/resources/${resourceId}/effective-permission`,
    );
    return unwrap(response.data).level;
  },

  async listPermissions(resourceId: string): Promise<PermissionGrant[]> {
    const response = await http.get<ApiEnvelope<{ items: PermissionGrant[] }>>(
      `/resources/${resourceId}/permissions`,
    );
    return unwrap(response.data).items;
  },

  async grant(resourceId: string, userId: string, permission: PermissionValue): Promise<PermissionGrant> {
    const response = await http.put<ApiEnvelope<PermissionGrant>>(
      `/resources/${resourceId}/permissions/${userId}`,
      { permission },
    );
    return unwrap(response.data);
  },

  async revoke(resourceId: string, userId: string): Promise<void> {
    await http.delete(`/resources/${resourceId}/permissions/${userId}`);
  },

  /** 创建分享链接：响应里的 token 是**明文，只出现这一次** */
  async createShare(resourceId: string, input: CreateShareInput): Promise<ShareLink> {
    const body: Record<string, unknown> = { permission: input.permission };
    if (input.expiresInSeconds) body.expires_in_seconds = input.expiresInSeconds;
    if (input.maxDownloads) body.max_downloads = input.maxDownloads;
    const response = await http.post<ApiEnvelope<ShareLink>>(
      `/resources/${resourceId}/shares`,
      body,
    );
    return unwrap(response.data);
  },

  async listShares(resourceId: string): Promise<ShareListing> {
    const response = await http.get<ApiEnvelope<ShareListing>>(
      `/resources/${resourceId}/shares`,
    );
    return unwrap(response.data);
  },

  async revokeShare(shareId: string): Promise<void> {
    await http.delete(`/shares/${shareId}`);
  },

  /** 「分享给我的」：别人授权给我、或分享给我的文件与文件夹 */
  async listSharedWithMe(): Promise<SharedWithMeListing> {
    const response = await http.get<ApiEnvelope<SharedWithMeListing>>('/shared-with-me');
    return unwrap(response.data);
  },

  /** 公开接口：不需要登录，用于分享页预览 */
  async describePublic(token: string): Promise<PublicShareView> {
    const response = await http.get<ApiEnvelope<PublicShareView>>(`/public/shares/${token}`);
    return unwrap(response.data);
  },

  buildPublicUrl(token: string): string {
    return `${window.location.origin}/s/${token}`;
  },
};
