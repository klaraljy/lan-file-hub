/** 与 docs/api.md 版本端点对应的类型。 */

export interface FileVersion {
  id: string;
  file_id: string;
  version_number: number;
  size_bytes: number;
  content_hash: string;
  created_by: string;
  created_at: string;
  is_current: boolean;
}
