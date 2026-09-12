/** 与 docs/api.md 上传端点对应的类型。 */

/** 后端 UploadStatus（docs/data-model.md §UploadSession） */
export type ServerUploadStatus =
  | 'INITIATED'
  | 'UPLOADING'
  | 'MERGING'
  | 'VERIFYING'
  | 'COMPLETED'
  | 'FAILED'
  | 'ABORTED'
  | 'EXPIRED';

/**
 * 前端上传任务状态（docs/frontend.md §4.2）。
 * 必须与页面状态（idle/loading/...）区分开，不能用 loading/error 两个值糊弄。
 */
export type UploadTaskStatus =
  | 'queued'
  | 'hashing'
  | 'initiated'
  | 'uploading'
  | 'paused'
  | 'retrying'
  | 'merging'
  | 'verifying'
  | 'completed'
  | 'failed'
  | 'cancelled'
  | 'expired';

export interface ExistingObjectInfo {
  file_id: string;
  name: string;
  size_bytes: number;
}

export interface CreateUploadResponse {
  upload_id: string;
  chunk_size: number;
  total_chunks: number;
  total_size: number;
  status: ServerUploadStatus;
  expires_at: string;
  completed_chunks: number[];
  /** 去重命中：该用户已有同样内容的文件，可以不传分块直接 complete */
  already_exists?: boolean;
  existing_file?: ExistingObjectInfo;
}

export interface UploadStatusResponse {
  upload_id: string;
  status: ServerUploadStatus;
  file_name: string;
  total_size: number;
  chunk_size: number;
  total_chunks: number;
  completed_chunks: number[];
  received_bytes: number;
  expires_at: string;
  error_code?: string;
  file_id?: string;
}

/** 未完成上传任务持久化到 IndexedDB 的部分（不保存文件内容） */
export interface PersistedUploadTask {
  uploadId: string;
  folderId: string;
  name: string;
  size: number;
  chunkSize: number;
  totalChunks: number;
  createdAt: number;
}

export interface UploadTask {
  /** 前端任务 id，用于 React key 与更新 */
  id: string;
  uploadId?: string;
  folderId: string;
  name: string;
  size: number;
  chunkSize: number;
  totalChunks: number;
  completedChunks: number[];
  status: UploadTaskStatus;
  /** 0–100 */
  percent: number;
  error?: string;
  /** 已经自动重试的次数 */
  attempts: number;
  /** 刷新后从后端恢复的任务：需要用户重新选择同一个文件才能继续 */
  needsFile?: boolean;
  /** 速度提示（字节/秒） */
  rate?: number;
}

export const TERMINAL_TASK_STATUSES: UploadTaskStatus[] = ['completed', 'failed', 'cancelled'];
