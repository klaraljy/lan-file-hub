/**
 * 文件预览（2026-10-03，用户要求：图片要有缩略图/预览）。
 *
 * 为什么预览要走**下载票据**：浏览器给 `<img src>` 发请求时**不带 Authorization 头**，
 * 所以受保护的下载地址直接用会 401。已有的一次性票据（60 秒、purpose=download、
 * 仍然每次都复查权限 —— AGENTS §15.2）正好解决这个问题：先换票据，再把票据拼进 URL。
 *
 * ⛔ 不要为此新增"公开的预览接口"：那等于绕过认证；也不要回到 `responseType: 'blob'`
 * （大图会把内存吃光，AGENTS §15.2 明令禁止）。
 */
import type { ApiEnvelope } from '../types/auth';
import { http } from './api';

export type PreviewKind = 'image' | 'pdf' | 'text' | 'none';

/** 预览能显示多大：太大的文本不值得塞进弹窗（用户真要就下载） */
export const MAX_TEXT_PREVIEW_BYTES = 256 * 1024;

/** 图片后缀兜底：有些上传的 mime 是 application/octet-stream，光看 mime 认不出来 */
const IMAGE_EXTENSIONS = [
  'png',
  'jpg',
  'jpeg',
  'gif',
  'webp',
  'bmp',
  'svg',
  'avif',
  'heic',
];
const TEXT_EXTENSIONS = [
  'txt',
  'md',
  'log',
  'json',
  'csv',
  'xml',
  'yaml',
  'yml',
  'ini',
  'conf',
  'html',
  'htm',
  'css',
  'js',
  'ts',
  'tsx',
  'jsx',
  'py',
  'c',
  'h',
  'cpp',
  'hpp',
  'java',
  'go',
  'rs',
  'sh',
  'ps1',
  'sql',
];

function extensionOf(name: string): string {
  const dot = name.lastIndexOf('.');
  if (dot < 0 || dot === name.length - 1) return '';
  return name.slice(dot + 1).toLowerCase();
}

/** 这个文件能不能预览、用哪种方式（纯函数，单测覆盖） */
export function previewKind(mimeType: string | undefined, fileName: string): PreviewKind {
  const mime = (mimeType ?? '').toLowerCase();
  const ext = extensionOf(fileName);
  if (mime.startsWith('image/') || IMAGE_EXTENSIONS.includes(ext)) return 'image';
  if (mime === 'application/pdf' || ext === 'pdf') return 'pdf';
  if (mime.startsWith('text/') || TEXT_EXTENSIONS.includes(ext)) return 'text';
  return 'none';
}

export function canPreview(mimeType: string | undefined, fileName: string): boolean {
  return previewKind(mimeType, fileName) !== 'none';
}

/** 换一张预览用的临时地址（票据在 URL 里，浏览器自己会去取） */
export async function fetchPreviewUrl(fileId: string): Promise<string> {
  const response = await http.post<ApiEnvelope<{ ticket: string; url: string; expires_in: number }>>(
    `/files/${fileId}/download-ticket`,
  );
  return response.data.data.url;
}

/** 文本预览：走带 Token 的请求把内容读回来（有大小上限，超了就让用户下载） */
export async function fetchTextPreview(fileId: string): Promise<string> {
  const response = await http.get<string>(`/files/${fileId}/download`, {
    responseType: 'text',
    transformResponse: [(data: unknown) => data],
    headers: { Accept: 'text/plain, */*' },
  });
  const text = typeof response.data === 'string' ? response.data : String(response.data ?? '');
  if (text.length > MAX_TEXT_PREVIEW_BYTES) {
    return text.slice(0, MAX_TEXT_PREVIEW_BYTES) + '\n\n……（文件太大，只显示前面一部分，完整内容请下载）';
  }
  return text;
}
