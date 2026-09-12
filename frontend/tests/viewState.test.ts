import { describe, expect, it } from 'vitest';

import { ApiError } from '../src/services/api';
import {
  PAGE_STATES,
  UPLOAD_STATES,
  derivePageState,
  isOfflineError,
  isPageState,
  isRetryableError,
  isUploadState,
  pageStateLabel,
  pageStateMessage,
  showsContent,
  showsEmptyPlaceholder,
  showsErrorPlaceholder,
  showsSpinner,
} from '../src/features/files/viewState';

/**
 * 这些用例守的是 docs/frontend.md §4.1 的要求：
 *   - 页面状态必须是那 7 个值，且能按明确优先级推导出来；
 *   - 页面状态与上传状态是两套枚举，不能互相顶替。
 */
describe('页面状态推导', () => {
  it('没有任何输入时是 idle', () => {
    expect(derivePageState()).toBe('idle');
    expect(derivePageState({})).toBe('idle');
  });

  it('首次加载且没有数据时是 loading', () => {
    expect(derivePageState({ isLoading: true })).toBe('loading');
    expect(derivePageState({ isLoading: true, isFetching: true })).toBe('loading');
  });

  it('后台刷新时保持 success，内容不被替换（列表不闪）', () => {
    expect(derivePageState({ isLoading: true, isFetching: true, hasData: true })).toBe('success');
    expect(derivePageState({ isFetching: true, hasData: true })).toBe('success');
  });

  it('有数据且不在请求中就是 success', () => {
    expect(derivePageState({ hasData: true })).toBe('success');
  });

  it('取消优先于一切：结果已经没人要了', () => {
    expect(derivePageState({ cancelled: true, isLoading: true, hasData: true })).toBe('cancelled');
    expect(derivePageState({ cancelled: true, isError: true })).toBe('cancelled');
  });

  it('断网单独成一态，比普通错误更具体', () => {
    const offline = new ApiError('NETWORK_ERROR', '无法连接服务器', '');
    expect(derivePageState({ isError: true, error: offline })).toBe('offline');
    // 断网且正在重试时仍然告诉用户是网络问题
    expect(derivePageState({ isError: true, error: offline, isFetching: true })).toBe('offline');
  });

  it('出错但正在重试是 retrying，不是 error', () => {
    const failure = new ApiError('STORAGE_UNAVAILABLE', '服务暂时不可用', '');
    expect(derivePageState({ isError: true, error: failure, isFetching: true })).toBe('retrying');
    expect(derivePageState({ isError: true, error: failure })).toBe('error');
  });

  it('没有 isError 标记时不会因为传了 error 就报错', () => {
    expect(derivePageState({ error: new Error('残留错误'), hasData: true })).toBe('success');
  });
});

describe('错误分类', () => {
  it('只有 NETWORK_ERROR 算断网', () => {
    expect(isOfflineError(new ApiError('NETWORK_ERROR', '断网', ''))).toBe(true);
    expect(isOfflineError(new ApiError('INTERNAL_ERROR', '服务端错误', ''))).toBe(false);
    expect(isOfflineError(new Error('普通异常'))).toBe(false);
    expect(isOfflineError(null)).toBe(false);
    expect(isOfflineError(undefined)).toBe(false);
  });

  it('可重试判定沿用 ApiError.retryable', () => {
    expect(isRetryableError(new ApiError('NETWORK_ERROR', '断网', ''))).toBe(true);
    expect(isRetryableError(new ApiError('STORAGE_UNAVAILABLE', '服务端忙', ''))).toBe(true);
    expect(isRetryableError(new ApiError('RESOURCE_CONFLICT', '同名', ''))).toBe(false);
    expect(isRetryableError(new ApiError('HASH_MISMATCH', '哈希不符', ''))).toBe(false);
    expect(isRetryableError(undefined)).toBe(false);
  });
});

describe('状态文案与展示开关', () => {
  it('七个状态都有中文标签', () => {
    for (const state of PAGE_STATES) {
      expect(pageStateLabel(state)).not.toBe('');
    }
  });

  it('错误文案优先用后端返回的 message', () => {
    const error = new ApiError('FOLDER_NOT_FOUND', '目标文件夹不存在', '');
    expect(pageStateMessage('error', error)).toBe('目标文件夹不存在');
    // 非 ApiError 的普通异常也用它的 message
    expect(pageStateMessage('error', new Error('自定义失败'))).toBe('自定义失败');
    // 什么都没有时给兜底文案
    expect(pageStateMessage('error')).toBe('加载失败，请稍后重试');
    expect(pageStateMessage('error', { weird: true })).toBe('加载失败，请稍后重试');
  });

  it('断网与取消各有明确文案', () => {
    expect(pageStateMessage('offline')).toContain('网络');
    expect(pageStateMessage('cancelled')).toContain('取消');
    expect(pageStateMessage('loading')).toContain('加载');
    expect(pageStateMessage('success')).toBe('');
  });

  it('展示开关与状态一一对应', () => {
    expect(showsContent('success')).toBe(true);
    expect(showsContent('loading')).toBe(false);
    expect(showsSpinner('loading')).toBe(true);
    expect(showsSpinner('retrying')).toBe(true);
    expect(showsSpinner('success')).toBe(false);
    expect(showsErrorPlaceholder('error')).toBe(true);
    expect(showsErrorPlaceholder('offline')).toBe(true);
    expect(showsErrorPlaceholder('success')).toBe(false);
    // 空目录只有「加载成功且一条都没有」时才算
    expect(showsEmptyPlaceholder('success', 0)).toBe(true);
    expect(showsEmptyPlaceholder('success', 3)).toBe(false);
    expect(showsEmptyPlaceholder('loading', 0)).toBe(false);
  });
});

describe('页面状态与上传状态必须区分', () => {
  it('页面状态就是文档里的 7 个值', () => {
    expect([...PAGE_STATES].sort()).toEqual(
      ['cancelled', 'error', 'idle', 'loading', 'offline', 'retrying', 'success'].sort(),
    );
  });

  it('上传状态是 §4.2 的 12 个值', () => {
    expect(UPLOAD_STATES).toHaveLength(12);
    for (const state of ['queued', 'hashing', 'uploading', 'merging', 'completed', 'failed']) {
      expect(isUploadState(state)).toBe(true);
    }
  });

  it('页面不可能处于「上传中」这类上传状态', () => {
    for (const uploadOnly of [
      'queued',
      'hashing',
      'initiated',
      'uploading',
      'paused',
      'merging',
      'verifying',
      'completed',
      'failed',
      'expired',
    ]) {
      expect(isPageState(uploadOnly)).toBe(false);
    }
  });

  it('上传任务也不会用 success / loading 这类页面状态表示', () => {
    expect(isUploadState('success')).toBe(false);
    expect(isUploadState('loading')).toBe(false);
    expect(isUploadState('idle')).toBe(false);
    expect(isUploadState('offline')).toBe(false);
  });

  it('retrying 与 cancelled 同名但分属两套枚举，说明里已交代语义不同', () => {
    // 同名不代表可以混用：判定函数各自只认自己那一套
    expect(isPageState('retrying')).toBe(true);
    expect(isUploadState('retrying')).toBe(true);
    // 而 success 只能由页面状态推导出来，uploadManager 里没有这个状态
    expect(isPageState('success')).toBe(true);
    expect(isUploadState('success')).toBe(false);
  });
});
