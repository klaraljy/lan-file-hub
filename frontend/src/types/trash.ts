/** 回收站相关类型（docs/api.md §回收站端点）。 */

export type TrashItemType = 'file' | 'folder';

export interface TrashItem {
  id: string;
  type: TrashItemType;
  name: string;
  size_bytes: number;
  original_folder_id: string;
  original_folder_name: string;
  deleted_at: string;
}

export interface TrashListing {
  items: TrashItem[];
  total: number;
  page: number;
  page_size: number;
  /** 回收站保留天数（超过后由后台任务永久删除） */
  retention_days: number;
}

export interface RestoreResult {
  id: string;
  type: TrashItemType;
  name: string;
  target_folder_id: string;
  /** 原目录已不可用，恢复到了用户根目录 */
  moved_to_root: boolean;
  /** 已经恢复过：幂等命中 */
  already_restored: boolean;
}

export interface PurgeResult {
  id: string;
  type: TrashItemType;
  freed_bytes: number;
  file_count: number;
  folder_count: number;
  /** 已交给后台任务异步删除的对象数量 */
  queued_objects: number;
  async: boolean;
}
