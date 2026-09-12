import type { ApiEnvelope } from '../types/auth';
import type { Folder, FolderListing, FolderPath } from '../types/folder';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

export interface ListChildrenParams {
  page?: number;
  page_size?: number;
  sort_by?: string;
  sort_order?: 'asc' | 'desc';
}

export const foldersApi = {
  /** 当前用户的根目录（后端会在缺失时自动补建） */
  async root(): Promise<Folder> {
    const response = await http.get<ApiEnvelope<Folder>>('/folders/root');
    return unwrap(response.data);
  },

  async get(folderId: string): Promise<Folder> {
    const response = await http.get<ApiEnvelope<Folder>>(`/folders/${folderId}`);
    return unwrap(response.data);
  },

  /** 从根到该目录的完整链路，用于面包屑 */
  async path(folderId: string): Promise<Folder[]> {
    const response = await http.get<ApiEnvelope<FolderPath>>(`/folders/${folderId}/path`);
    return unwrap(response.data).items;
  },

  async children(folderId: string, params: ListChildrenParams = {}): Promise<FolderListing> {
    const response = await http.get<ApiEnvelope<FolderListing>>(`/folders/${folderId}/children`, {
      params,
    });
    return unwrap(response.data);
  },

  async create(parentId: string, name: string): Promise<Folder> {
    const response = await http.post<ApiEnvelope<Folder>>('/folders', {
      parent_id: parentId,
      name,
    });
    return unwrap(response.data);
  },

  async rename(folderId: string, name: string): Promise<Folder> {
    const response = await http.patch<ApiEnvelope<Folder>>(`/folders/${folderId}`, { name });
    return unwrap(response.data);
  },

  async move(folderId: string, parentId: string): Promise<Folder> {
    const response = await http.patch<ApiEnvelope<Folder>>(`/folders/${folderId}`, {
      parent_id: parentId,
    });
    return unwrap(response.data);
  },

  async remove(folderId: string): Promise<void> {
    await http.delete(`/folders/${folderId}`);
  },
};
