/** 与 docs/api.md 文件夹端点对应的类型。 */

export type FolderStatus = 'ACTIVE' | 'TRASHED' | 'PURGED';

export interface Folder {
  id: string;
  owner_id: string;
  parent_id: string | null;
  name: string;
  status: FolderStatus;
  is_root: boolean;
  created_at: string;
  updated_at: string;
  deleted_at: string | null;
}

export interface FolderListing {
  items: Folder[];
  total: number;
  page: number;
  page_size: number;
}

export interface FolderPath {
  items: Folder[];
}

/** 排序字段白名单，必须与后端 BuildOrderBy 的白名单一致 */
export const FOLDER_SORT_FIELDS = ['name', 'created_at', 'updated_at'] as const;
export type FolderSortField = (typeof FOLDER_SORT_FIELDS)[number];
