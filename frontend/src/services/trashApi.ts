/** 回收站与删除接口（阶段八）。 */
import type { ApiEnvelope } from '../types/auth';
import type { PurgeResult, RestoreResult, TrashListing, TrashItemType } from '../types/trash';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

export interface ListTrashParams {
  page?: number;
  page_size?: number;
  /** 按类型筛选；不传表示全部 */
  type?: TrashItemType;
}

export const trashApi = {
  async list(params: ListTrashParams = {}): Promise<TrashListing> {
    const response = await http.get<ApiEnvelope<TrashListing>>('/trash', { params });
    return unwrap(response.data);
  },

  /** 恢复；幂等。原目录不可用时后端会放回用户根目录，并在响应里说明。 */
  async restore(resourceId: string): Promise<RestoreResult> {
    const response = await http.post<ApiEnvelope<RestoreResult>>(
      `/trash/${resourceId}/restore`,
    );
    return unwrap(response.data);
  },

  /** 永久删除：后端只删数据库记录并把对象交给后台任务，不会长时间阻塞请求。 */
  async purge(resourceId: string): Promise<PurgeResult> {
    const response = await http.delete<ApiEnvelope<PurgeResult>>(`/trash/${resourceId}/purge`);
    return unwrap(response.data);
  },
};

export const deleteFileApi = {
  /** 软删除：文件进入回收站，可恢复 */
  async softDelete(fileId: string): Promise<void> {
    await http.delete(`/files/${fileId}`);
  },
};
