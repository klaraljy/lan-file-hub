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

export type PreviewKind = 'image' | 'pdf' | 'text' | 'video' | 'audio' | 'office' | 'none';

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
const VIDEO_EXTENSIONS = [
  'mp4',
  'm4v',
  'webm',
  'ogv',
  'mov',
  'mkv',
  'avi',
  'wmv',
  'flv',
  'ts',
  'm2ts',
  '3gp',
];
const AUDIO_EXTENSIONS = ['mp3', 'wav', 'flac', 'aac', 'm4a', 'ogg', 'oga', 'opus', 'wma'];
/**
 * 浏览器自己就能播的容器（2026-10-03）。
 * 手机拍的、网页下载的绝大多数是 mp4；mov/mkv/avi/wmv/flv 浏览器不支持 —— 那就老实说
 * "浏览器播不了，下载后用本机播放器打开"，⛔ 不要摆一个放不出来的播放器骗人。
 */
const PLAYABLE_EXTENSIONS = ['mp4', 'm4v', 'webm', 'ogv', 'mp3', 'wav', 'm4a', 'aac', 'oga', 'ogg'];
/** Word / Excel / PPT（含 WPS 的格式）：网页里不渲染，只给"下载后用本机软件打开"的说明 */
const OFFICE_EXTENSIONS = [
  'doc',
  'docx',
  'xls',
  'xlsx',
  'ppt',
  'pptx',
  'wps',
  'et',
  'dps',
  'rtf',
  'odt',
  'ods',
  'odp',
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
  if (mime.startsWith('video/') || VIDEO_EXTENSIONS.includes(ext)) return 'video';
  if (mime.startsWith('audio/') || AUDIO_EXTENSIONS.includes(ext)) return 'audio';
  const office =
    OFFICE_EXTENSIONS.includes(ext) ||
    mime.includes('word') ||
    mime.includes('excel') ||
    mime.includes('spreadsheet') ||
    mime.includes('presentation') ||
    mime.includes('powerpoint') ||
    mime.includes('officedocument') ||
    mime === 'application/rtf';
  if (office) return 'office';
  if (mime.startsWith('text/') || TEXT_EXTENSIONS.includes(ext)) return 'text';
  return 'none';
}

export function canPreview(mimeType: string | undefined, fileName: string): boolean {
  return previewKind(mimeType, fileName) !== 'none';
}

/** 视频/音频能不能直接在网页里播（不能就让用户下载） */
export function canPlayInBrowser(mimeType: string | undefined, fileName: string): boolean {
  const kind = previewKind(mimeType, fileName);
  if (kind !== 'video' && kind !== 'audio') return false;
  const ext = extensionOf(fileName);
  if (ext) return PLAYABLE_EXTENSIONS.includes(ext);
  // 没有扩展名时只能看 mime
  const mime = (mimeType ?? '').toLowerCase();
  return ['video/mp4', 'video/webm', 'audio/mpeg', 'audio/mp4', 'audio/wav', 'audio/ogg'].includes(
    mime,
  );
}

/** 看不了时，界面必须给出**能照做**的一句话（AGENTS §14.1：界面不许骗人，也不许只说"不支持"） */
export function previewHint(kind: PreviewKind, fileName: string): string {
  switch (kind) {
    case 'office':
      return `「${fileName}」是 Word / Excel / PPT 这类文档，网页里没法直接显示。点下面的「下载」保存到设备上，电脑上双击就能用 Office / WPS 打开，手机上用 WPS 之类的应用打开。`;
    case 'video':
    case 'audio':
      return `「${fileName}」这个格式浏览器播不了（mp4 / webm / mp3 可以直接播）。点下面的「下载」保存到设备上，用本机的播放器打开。`;
    default:
      return '这种文件不能在网页里直接打开（能直接看的是图片、视频、音频、PDF 和文本）。点下面的「下载」保存到设备上看。';
  }
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
