/** 与 docs/api.md 文件端点对应的类型。 */

export type FileStatus = 'ACTIVE' | 'TRASHED' | 'PURGED';

export interface FileMeta {
  id: string;
  owner_id: string;
  folder_id: string;
  current_version_id: string | null;
  name: string;
  mime_type: string;
  size_bytes: number;
  content_hash: string;
  status: FileStatus;
  created_at: string;
  updated_at: string;
  deleted_at: string | null;
}

export interface FileListing {
  items: FileMeta[];
  total: number;
  page: number;
  page_size: number;
}
