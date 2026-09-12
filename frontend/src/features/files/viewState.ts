/**
 * 页面状态机（docs/frontend.md §4.1）。
 *
 * 页面状态与上传状态（§4.2）是**两套互不相干的枚举**：
 *   - 页面状态描述「这一屏数据现在是什么情况」，只有这里列出的 7 个值；
 *   - 上传状态描述「某个上传任务走到哪一步了」，见 types/upload.ts。
 * 两者绝不能互相顶替（例如用 page=loading 表示「上传中」），
 * 所以下面额外提供 isPageState / isUploadState 两个判定给测试与调试用。
 *
 * 本文件必须保持**纯函数**：不碰网络、不碰 DOM，方便直接单测。
 */
import { ApiError } from '../../services/api';

export type PageState =
  | 'idle'
  | 'loading'
  | 'success'
  | 'error'
  | 'retrying'
  | 'offline'
  | 'cancelled';

export const PAGE_STATES: readonly PageState[] = [
  'idle',
  'loading',
  'success',
  'error',
  'retrying',
  'offline',
  'cancelled',
];

/** 上传任务的 12 个状态（docs/frontend.md §4.2），只用于「两者必须区分」的判定 */
export const UPLOAD_STATES: readonly string[] = [
  'queued',
  'hashing',
  'initiated',
  'uploading',
  'paused',
  'retrying',
  'merging',
  'verifying',
  'completed',
  'failed',
  'cancelled',
  'expired',
];

export interface PageStateInput {
  /** 首次加载中（还没有任何数据可展示） */
  isLoading?: boolean;
  /** 请求进行中（含后台刷新） */
  isFetching?: boolean;
  isError?: boolean;
  error?: unknown;
  /** 是否已经有数据（有数据时刷新不应该把内容换成骨架屏） */
  hasData?: boolean;
  /** 用户主动取消（例如切换目录导致上一次请求作废） */
  cancelled?: boolean;
}

/** 网络层不可用（断网、后端没起来、超时） */
export function isOfflineError(error: unknown): boolean {
  if (!error) return false;
  if (error instanceof ApiError) return error.code === 'NETWORK_ERROR';
  return (
    typeof error === 'object' &&
    error !== null &&
    (error as { code?: unknown }).code === 'NETWORK_ERROR'
  );
}

/** 是否值得重试：只有网络类与服务端暂时不可用才值得（docs/frontend.md §5.4） */
export function isRetryableError(error: unknown): boolean {
  if (!error) return false;
  if (error instanceof ApiError) return error.retryable;
  return isOfflineError(error);
}

/**
 * 推导页面状态。优先级（从高到低）：
 *   cancelled → offline → retrying → error → loading → success → idle
 *
 * 说明：
 *   - 已经取消的请求不再关心结果；
 *   - 断网比普通错误更具体，单独成一态，方便给出「检查网络」的提示；
 *   - 出错但正在重试时是 retrying，让用户知道还有希望；
 *   - 有数据时后台刷新保持 success，内容不被替换（列表不闪）。
 */
export function derivePageState(input: PageStateInput = {}): PageState {
  const { isLoading, isFetching, isError, error, hasData, cancelled } = input;

  if (cancelled) return 'cancelled';
  if (isError) {
    if (isOfflineError(error)) return 'offline';
    if (isFetching) return 'retrying';
    return 'error';
  }
  if (isLoading && !hasData) return 'loading';
  if (hasData) return 'success';
  if (isFetching) return 'loading';
  return 'idle';
}

/** 状态的一句话标签，给 Tag / 骨架屏用 */
export function pageStateLabel(state: PageState): string {
  switch (state) {
    case 'idle':
      return '未加载';
    case 'loading':
      return '加载中';
    case 'success':
      return '已加载';
    case 'error':
      return '加载失败';
    case 'retrying':
      return '正在重试';
    case 'offline':
      return '网络不可用';
    case 'cancelled':
      return '已取消';
  }
}

/**
 * 状态对应的用户可理解文案。
 * 有后端返回的 message 时优先用它（后端文案已经是中文且可展示）。
 */
export function pageStateMessage(state: PageState, error?: unknown): string {
  switch (state) {
    case 'idle':
      return '还没有加载内容';
    case 'loading':
      return '正在加载…';
    case 'success':
      return '';
    case 'retrying':
      return '请求失败，正在重试…';
    case 'offline':
      return '网络已断开，请检查网络后重试';
    case 'cancelled':
      return '已取消加载';
    case 'error':
      return errorText(error) ?? '加载失败，请稍后重试';
  }
}

function errorText(error: unknown): string | null {
  if (error instanceof ApiError) return error.message || null;
  if (error instanceof Error) return error.message || null;
  return null;
}

/** 是否应该展示内容（表格 / 网格） */
export function showsContent(state: PageState): boolean {
  return state === 'success';
}

/** 是否应该展示加载指示器 */
export function showsSpinner(state: PageState): boolean {
  return state === 'loading' || state === 'retrying';
}

/** 是否应该展示「加载失败」占位 */
export function showsErrorPlaceholder(state: PageState): boolean {
  return state === 'error' || state === 'offline';
}

/** 是否应该展示「空目录」提示（成功但一条数据都没有） */
export function showsEmptyPlaceholder(state: PageState, rowCount: number): boolean {
  return state === 'success' && rowCount === 0;
}

/** 运行期判定：某个字符串是不是页面状态 */
export function isPageState(value: string): value is PageState {
  return (PAGE_STATES as readonly string[]).includes(value);
}

/** 运行期判定：某个字符串是不是上传状态（与页面状态必须区分） */
export function isUploadState(value: string): boolean {
  return UPLOAD_STATES.includes(value);
}
