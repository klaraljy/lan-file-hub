import type { ApiEnvelope } from '../types/auth';
import type { FileListing, FileMeta } from '../types/file';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

/**
 * 大文件流式下载：先换一张 **60 秒有效的下载票据**，再让**浏览器自己**去下载。
 *
 * 为什么这么做：
 *   - 下载接口需要 Authorization 头，浏览器直接导航带不上它；
 *   - 用 axios 取 blob 会把**整个文件读进浏览器内存** —— 几 GB 的文件必然崩（用户实测需求是 5 GB）。
 * 票据方案下浏览器是**流式落盘**的，内存占用与文件大小无关；票据 60 秒过期、只对这一个文件有效，
 * 而且后端在签发和下载时都会各自校验一次权限。
 */
async function downloadWithTicket(ticketPath: string, fileName: string): Promise<void> {
  const response = await http.post<ApiEnvelope<{ url: string }>>(ticketPath);
  triggerBrowserDownload(unwrap(response.data).url, fileName);
}

/** 用隐藏的 <a download> 触发浏览器原生下载（流式落盘，不进内存） */
export function triggerBrowserDownload(url: string, fileName: string): void {
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = fileName;
  anchor.rel = 'noopener';
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
}

export interface ListFilesParams {
  page?: number;
  page_size?: number;
  sort_by?: string;
  sort_order?: 'asc' | 'desc';
}

/** 「最近文件」里的一条：除了文件本身，还带它在哪个文件夹（不带位置用户不知道去哪找） */
export interface RecentFileItem extends FileMeta {
  folder_id: string;
  /** 根目录时为空串，界面显示「我的文件」 */
  folder_name: string;
}

export interface RecentFileListing {
  items: RecentFileItem[];
  total: number;
  page: number;
  page_size: number;
}

export const filesApi = {
  async list(folderId: string, params: ListFilesParams = {}): Promise<FileListing> {
    const response = await http.get<ApiEnvelope<FileListing>>('/files', {
      params: { folder_id: folderId, ...params },
    });
    return unwrap(response.data);
  },

  /** 最近文件：跨目录按更新时间倒序（后端 /api/v1/recent-files，2026-10-03） */
  async listRecent(params: { page?: number; page_size?: number } = {}): Promise<RecentFileListing> {
    const response = await http.get<ApiEnvelope<RecentFileListing>>('/recent-files', { params });
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
   * 下载（**流式落盘**，支持几 GB 的大文件）。
   * 先取 60 秒票据，再让浏览器自己下载 —— 见 downloadWithTicket 的说明。
   */
  async download(file: FileMeta): Promise<void> {
    await downloadWithTicket(`/files/${file.id}/download-ticket`, file.name);
  },

  /**
   * 只知道 id 与文件名时下载（例如「分享给我的」列表里没有完整的文件元数据）。
   * 与 download 走同一条路径，避免两处各写一份下载逻辑。
   */
  async downloadById(fileId: string, fileName: string): Promise<void> {
    await downloadWithTicket(`/files/${fileId}/download-ticket`, fileName);
  },
};
