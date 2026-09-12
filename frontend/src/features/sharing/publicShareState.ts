/**
 * 公开分享页（/s/:token）的状态映射与文案（docs/permissions.md §7）。
 *
 * 公开页不登录、不依赖任何鉴权上下文，所以状态判定单独放一个纯模块：
 *   输入是「查询的三个布尔量 + 错误对象」，输出是要展示哪种界面。
 * 失效原因（不存在 / 已撤销 / 已过期 / 次数用完）**优先用后端返回的文案**，
 * 后端没给可用文案时才退回本地兜底文案。
 */
import { ApiError } from '../../services/api';
import type { PublicShareView } from '../../types/share';

export type PublicShareState = 'loading' | 'ready' | 'blocked' | 'error';

export interface PublicShareStateInput {
  isLoading?: boolean;
  isError?: boolean;
  error?: unknown;
  data?: PublicShareView | null;
}

/**
 * ready   —— 可以展示详情（能下载就带下载按钮）
 * blocked —— 分享仍然存在但不允许下载（过期 / 撤销 / 次数用完）
 * error   —— 链接无效或网络问题
 */
export function mapPublicShareState(input: PublicShareStateInput): PublicShareState {
  const { isLoading, isError, error, data } = input;
  if (isError) {
    // 资源类错误（404/410）在下面用文案区分，这里统一算「取不到分享」
    void error;
    return 'error';
  }
  if (isLoading && !data) return 'loading';
  if (!data) return 'loading';
  if (!data.can_download && data.resource_type === 'file') return 'blocked';
  return 'ready';
}

/** 链接失效时给用户的解释，尽量具体 */
export function publicShareErrorMessage(error: unknown): string {
  if (error instanceof ApiError) {
    // 分享链接不存在 / 已过期 / 已被撤销，后端统一按「查不到」返回（避免用状态码泄露链接是否存在），
    // 所以这里也把三种可能一起说清楚
    if (error.code === 'RESOURCE_NOT_FOUND') {
      return '这个分享链接不存在、已过期或已被撤销';
    }
    if (error.code === 'RATE_LIMITED') {
      return '访问过于频繁，请稍后再试';
    }
    if (error.code === 'NETWORK_ERROR') {
      return '网络已断开，请检查网络后重试';
    }
    // 其它错误码直接用后端文案：错误码是封闭集合（docs/api.md），前端不自造分类
    return error.message || '无法打开这个分享链接';
  }
  if (error instanceof Error && error.message) {
    return error.message;
  }
  return '无法打开这个分享链接';
}

/** 剩余下载次数文案；未设上限时说明「不限次数」 */
export function downloadQuotaLabel(view: PublicShareView): string {
  if (view.max_downloads === null) {
    return `已下载 ${view.download_count} 次（不限次数）`;
  }
  const remaining = Math.max(0, view.max_downloads - view.download_count);
  return `已下载 ${view.download_count} / ${view.max_downloads} 次，剩余 ${remaining} 次`;
}

/** 到期时间文案 */
export function expiryLabel(view: PublicShareView): string {
  if (!view.expires_at) return '长期有效';
  const time = Date.parse(view.expires_at);
  if (Number.isNaN(time)) return '有效期未知';
  if (time <= Date.now()) return '已过期';
  return `到期时间：${new Date(time).toLocaleString()}`;
}

/** 公开下载直链：公开接口不需要 Authorization 头，可以直接用浏览器导航下载 */
export function publicDownloadUrl(token: string): string {
  return `/api/v1/public/shares/${encodeURIComponent(token)}/download`;
}
