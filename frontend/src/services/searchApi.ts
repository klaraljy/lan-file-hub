/**
 * 搜索（2026-10-03）：`GET /api/v1/search?q=&page=&page_size=`
 *
 * 只搜自己的文件与文件夹（后端强制 owner 过滤）；关键字里的 `%`/`_` 由后端当普通字符处理。
 */
import type { ApiEnvelope } from '../types/auth';
import { http } from './api';
import type { FileMeta } from '../types/file';

export interface SearchFolder {
  id: string;
  name: string;
  parent_id: string;
}

/** 搜索结果里的文件比普通列表多一个字段：在哪个文件夹（否则用户不知道去哪儿找） */
export interface SearchFile extends FileMeta {
  folder_name?: string;
}

export interface SearchPayload {
  items: SearchFile[];
  folders: SearchFolder[];
  total: number;
  page: number;
  page_size: number;
  query: string;
}

export async function searchFiles(query: string, page = 1, pageSize = 50): Promise<SearchPayload> {
  const response = await http.get<ApiEnvelope<SearchPayload>>('/search', {
    params: { q: query, page, page_size: pageSize },
  });
  return response.data.data;
}
