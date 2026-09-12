/** 上传相关接口。分块走 XHR（Axios 的 onUploadProgress 依赖它）。 */
import type { ApiEnvelope } from '../types/auth';
import type { FileMeta } from '../types/file';
import type { CreateUploadResponse, UploadStatusResponse } from '../types/upload';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

export interface CreateUploadInput {
  folderId: string;
  name: string;
  size: number;
  mimeType?: string;
  /** 整个文件的 SHA-256；大文件用流式哈希算出来 */
  sha256?: string;
  /** 不传则用服务端默认（8 MiB） */
  chunkSize?: number;
}

export const uploadsApi = {
  async create(input: CreateUploadInput): Promise<CreateUploadResponse> {
    const body: Record<string, unknown> = {
      folder_id: input.folderId,
      file_name: input.name,
      size_bytes: input.size,
    };
    if (input.mimeType) body.mime_type = input.mimeType;
    if (input.sha256) body.sha256 = input.sha256;
    if (input.chunkSize) body.chunk_size = input.chunkSize;
    const response = await http.post<ApiEnvelope<CreateUploadResponse>>('/uploads', body);
    return unwrap(response.data);
  },

  async status(uploadId: string): Promise<UploadStatusResponse> {
    const response = await http.get<ApiEnvelope<UploadStatusResponse>>(`/uploads/${uploadId}`);
    return unwrap(response.data);
  },

  /**
   * 上传一个分块。请求体是原始字节，不是 multipart。
   * X-Chunk-Sha256 让服务端能立刻判定「重复分块内容是否一致」。
   */
  async putChunk(
    uploadId: string,
    index: number,
    data: Blob,
    chunkSha256: string,
    options: { onProgress?: (loaded: number) => void; signal?: AbortSignal } = {},
  ): Promise<void> {
    await http.put(`/uploads/${uploadId}/chunks/${index}`, data, {
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-Chunk-Sha256': chunkSha256,
      },
      signal: options.signal,
      onUploadProgress: (event) => {
        if (options.onProgress) {
          options.onProgress(event.loaded);
        }
      },
    });
  },

  async complete(uploadId: string): Promise<FileMeta> {
    const response = await http.post<ApiEnvelope<FileMeta>>(`/uploads/${uploadId}/complete`);
    return unwrap(response.data);
  },

  async cancel(uploadId: string): Promise<void> {
    await http.delete(`/uploads/${uploadId}`);
  },
};

export type UploadsApi = typeof uploadsApi;
