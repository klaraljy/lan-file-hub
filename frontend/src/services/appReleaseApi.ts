/**
 * 「配套手机应用」（安卓安装包）—— 用户 2026-10-03 要求：
 * 网页「我的」里要有一个入口，**永远指向最新那个 APK**，而且"改完就更新的"。
 *
 * 分工：
 *   - 版本 / 大小 / 构建时间 / 校验和都**不写死在前端**，一律问后端（`/api/v1/app/release`），
 *     后端每次现看磁盘上那个文件（见 `backend/src/application/app_release.cpp`）；
 *   - 下载地址用 `publicUrl.ts` 那套"手机点得开"的规则拼（和分享链接同一套），
 *     这样电脑上用 127.0.0.1 打开时，二维码里也是局域网地址。
 */
import type { ApiEnvelope } from '../types/auth';
import { http } from './api';
import { buildAppDownloadUrl } from './publicUrl';
import type { ShareUrlLocation } from './publicUrl';

export interface AppReleaseInfo {
  available: boolean;
  file_name: string;
  version_name: string;
  version_code: number;
  size_bytes: number;
  built_at: string;
  sha256: string;
  download_path: string;
}

export const EMPTY_APP_RELEASE: AppReleaseInfo = {
  available: false,
  file_name: '',
  version_name: '',
  version_code: 0,
  size_bytes: 0,
  built_at: '',
  sha256: '',
  download_path: '/api/v1/app/download',
};

/** 拉一次安装包信息（失败时按"没有安装包"处理，页面上给出人话提示，不弹错误） */
export async function fetchAppRelease(): Promise<AppReleaseInfo> {
  try {
    const response = await http.get<ApiEnvelope<AppReleaseInfo>>('/app/release');
    return normalizeAppRelease(response.data.data);
  } catch {
    return EMPTY_APP_RELEASE;
  }
}

/** 安装包信息里的空值/异常值兜底：后端给了空串也不能让界面显示 "undefined" */
export function normalizeAppRelease(raw: Partial<AppReleaseInfo> | undefined): AppReleaseInfo {
  return { ...EMPTY_APP_RELEASE, ...(raw ?? {}) };
}

/** 下载地址（局域网可达那条）；缺字段也照样给出默认路径。location 只为单测可注入。 */
export function appDownloadUrl(
  info: Partial<AppReleaseInfo> | undefined,
  location?: ShareUrlLocation,
): { url: string; warning?: string } {
  return buildAppDownloadUrl(info?.download_path || EMPTY_APP_RELEASE.download_path, location);
}

/** 人类可读的体积：APK 只有几百 KB ~ 几 MB，所以用 KB/MB 两档 */
export function formatSize(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '';
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

/**
 * 把后端给的 UTC 时间（`2026-10-03T09:01:18Z`）转成"看得懂"的本地时间。
 * 解析不出来就原样返回（宁可显示原始串，也不要显示 NaN 或 "Invalid Date"）。
 */
export function formatBuildTime(iso: string): string {
  if (!iso) return '';
  const parsed = new Date(iso);
  if (Number.isNaN(parsed.getTime())) return iso;
  const pad = (value: number) => String(value).padStart(2, '0');
  return (
    `${parsed.getFullYear()}-${pad(parsed.getMonth() + 1)}-${pad(parsed.getDate())} ` +
    `${pad(parsed.getHours())}:${pad(parsed.getMinutes())}`
  );
}

/** 卡片上那行"版本 · 构建号 · 大小 · 时间"（缺什么就少显示什么，不出现空占位） */
export function describeAppRelease(info: AppReleaseInfo): string {
  const parts: string[] = [];
  if (info.version_name) parts.push(`版本 ${info.version_name}`);
  // 构建号（2026-10-03）：每次出包 +1，手机上 App 首页显示同一个号，
  // 用户据此判断"手机上装的是不是最新"（覆盖安装是否生效）。
  if (info.version_code > 0) parts.push(`构建号 ${info.version_code}`);
  const size = formatSize(info.size_bytes);
  if (size) parts.push(size);
  const built = formatBuildTime(info.built_at);
  if (built) parts.push(`${built} 构建`);
  return parts.join(' · ');
}
