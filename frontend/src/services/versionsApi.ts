/**
 * 文件版本接口（docs/api.md §版本端点）。
 *
 * 版本是**只增不减**的：上传新内容与恢复历史版本都会追加一个新版本，
 * 旧版本的对象不会被覆盖，所以历史版本始终可下载、可再次恢复。
 */
import type { ApiEnvelope } from '../types/auth';
import type { FileMeta } from '../types/file';
import type { FileVersion } from '../types/version';
import { http } from './api';
import { triggerBrowserDownload } from './filesApi';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

export interface VersionListing {
  items: FileVersion[];
  total: number;
}

export const versionsApi = {
  async list(fileId: string): Promise<VersionListing> {
    const response = await http.get<ApiEnvelope<VersionListing>>(`/files/${fileId}/versions`);
    return unwrap(response.data);
  },

  /**
   * 下载指定历史版本。
   * 与当前版本一样走**票据 + 浏览器原生下载**（流式落盘，大文件不会进内存）；
   * 文件名加上 .v<版本号> 后缀，避免和当前版本混淆。
   */
  async download(file: FileMeta, version: FileVersion): Promise<void> {
    const response = await http.post<ApiEnvelope<{ url: string }>>(
      `/files/${file.id}/versions/${version.id}/download-ticket`,
    );
    triggerBrowserDownload(unwrap(response.data).url, `${file.name}.v${version.version_number}`);
  },

  /**
   * 恢复到历史版本。恢复本身也是追加一个新版本，不会丢数据。
   * If-Match 传当前版本 id：如果期间被别人改过，后端返回 409 FILE_VERSION_CONFLICT，
   * 响应 details 里带 current_version_id，调用方据此提示用户刷新。
   */
  async restore(fileId: string, versionId: string, currentVersionId: string | null): Promise<FileMeta> {
    const headers: Record<string, string> = {};
    if (currentVersionId) {
      headers['If-Match'] = currentVersionId;
    }
    const response = await http.post<ApiEnvelope<FileMeta>>(
      `/files/${fileId}/versions/${versionId}/restore`,
      undefined,
      { headers },
    );
    return unwrap(response.data);
  },
};
