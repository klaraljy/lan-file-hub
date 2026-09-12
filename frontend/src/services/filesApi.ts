import type { ApiEnvelope } from '../types/auth';
import type { FileListing, FileMeta } from '../types/file';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

/**
 * 取回文件内容并触发浏览器下载。
 *
 * 为什么不用 <a href>：下载接口需要 Authorization 头，浏览器直接导航带不上它。
 * 已知限制：整文件会先进入浏览器内存（大文件流式下载属于后续阶段）。
 */
async function downloadBlob(url: string, fileName: string): Promise<void> {
  const response = await http.get(url, { responseType: 'blob' });
  const objectUrl = URL.createObjectURL(response.data as Blob);
  const anchor = document.createElement('a');
  anchor.href = objectUrl;
  anchor.download = fileName;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  // 交给浏览器完成下载后再释放
  setTimeout(() => URL.revokeObjectURL(objectUrl), 60_000);
}

export interface ListFilesParams {
  page?: number;
  page_size?: number;
  sort_by?: string;
  sort_order?: 'asc' | 'desc';
}

export const filesApi = {
  async list(folderId: string, params: ListFilesParams = {}): Promise<FileListing> {
    const response = await http.get<ApiEnvelope<FileListing>>('/files', {
      params: { folder_id: folderId, ...params },
    });
    return unwrap(response.data);
  },

  async metadata(fileId: string): Promise<FileMeta> {
    const response = await http.get<ApiEnvelope<FileMeta>>(`/files/${fileId}`);
    return unwrap(response.data);
  },

  /**
   * 上传单个文件。
   * 请求体直接是文件内容（不是 multipart），后端流式写入并计算 SHA-256；
   * 文件名与目标目录走查询参数。
   */
  async upload(
    folderId: string,
    file: File,
    onProgress?: (percent: number) => void,
  ): Promise<FileMeta> {
    const response = await http.post<ApiEnvelope<FileMeta>>('/files', file, {
      params: { folder_id: folderId, name: file.name },
      headers: { 'Content-Type': file.type || 'application/octet-stream' },
      onUploadProgress: (event) => {
        if (!onProgress) return;
        const total = event.total ?? 0;
        if (total > 0) {
          onProgress(Math.min(100, Math.round((event.loaded / total) * 100)));
        }
      },
    });
    return unwrap(response.data);
  },

  /**
   * 下载。接口需要 Authorization 头，所以不能直接用 <a href>；
   * 这里取回 blob 再触发浏览器下载。
   *
   * 已知限制：整文件会先进入浏览器内存。大文件的分块/流式下载留到阶段十，
   * 与「改用 HttpOnly Cookie」一起处理（见 README「已知限制」）。
   */
  async download(file: FileMeta): Promise<void> {
    await downloadBlob(`/files/${file.id}/download`, file.name);
  },

  /**
   * 只知道 id 与文件名时下载（例如「分享给我的」列表里没有完整的文件元数据）。
   * 与 download 走同一条路径，避免两处各写一份下载逻辑。
   */
  async downloadById(fileId: string, fileName: string): Promise<void> {
    await downloadBlob(`/files/${fileId}/download`, fileName);
  },
};
