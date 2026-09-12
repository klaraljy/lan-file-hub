/** 权限与分享类型（docs/api.md §权限端点 / §分享端点）。 */

export type PermissionValue = 'READ' | 'WRITE' | 'OWNER';
export type EffectiveLevel = PermissionValue | 'ADMIN' | 'NONE';
export type SharePermission = 'READ' | 'WRITE';

export interface PermissionGrant {
  id: string;
  resource_type: 'file' | 'folder';
  resource_id: string;
  subject_type: 'USER';
  subject_id: string;
  subject_name: string;
  permission: PermissionValue;
  created_at: string;
  updated_at: string;
}

export interface ShareLink {
  id: string;
  resource_type: 'file' | 'folder';
  resource_id: string;
  resource_name: string;
  permission: SharePermission;
  download_count: number;
  created_by: string;
  created_at: string;
  revoked: boolean;
  revoked_at: string | null;
  expires_at: string | null;
  max_downloads: number | null;
  last_access_at: string | null;
  /** 只在创建响应里出现一次 */
  token?: string;
  url?: string;
  download_allowed?: boolean;
  token_visible_once?: boolean;
}

export interface ShareListing {
  items: ShareLink[];
  total: number;
}

/** 「分享给我的」列表项（GET /api/v1/shared-with-me） */
export interface SharedWithMeItem {
  resource_type: 'file' | 'folder';
  resource_id: string;
  name: string;
  size_bytes: number;
  permission: PermissionValue;
  owner_id: string;
  granted_at: string;
}

export interface SharedWithMeListing {
  items: SharedWithMeItem[];
  total: number;
}

/** 「分享给我的」列表的类型筛选：全部 / 只看文件 / 只看文件夹 */
export type SharedWithMeFilter = 'all' | 'file' | 'folder';

export interface PublicShareView {
  name: string;
  resource_type: 'file' | 'folder';
  size_bytes: number;
  mime_type: string;
  permission: SharePermission;
  expires_at: string | null;
  download_count: number;
  max_downloads: number | null;
  can_download: boolean;
  download_blocked_reason: string;
}
