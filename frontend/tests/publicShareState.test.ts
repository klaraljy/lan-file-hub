import { describe, expect, it } from 'vitest';

import { ApiError } from '../src/services/api';
import {
  downloadQuotaLabel,
  expiryLabel,
  mapPublicShareState,
  publicDownloadUrl,
  publicShareErrorMessage,
} from '../src/features/sharing/publicShareState';
import type { PublicShareView } from '../src/types/share';

/**
 * 公开分享页是唯一不需要登录的页面，失效原因必须让陌生人也能看懂，
 * 所以状态映射与文案单独用纯函数覆盖。
 */

function makeView(overrides: Partial<PublicShareView> = {}): PublicShareView {
  return {
    name: '报表.pdf',
    resource_type: 'file',
    size_bytes: 2048,
    mime_type: 'application/pdf',
    permission: 'READ',
    expires_at: null,
    download_count: 0,
    max_downloads: null,
    can_download: true,
    download_blocked_reason: '',
    ...overrides,
  };
}

describe('mapPublicShareState', () => {
  it('加载中且没有数据是 loading', () => {
    expect(mapPublicShareState({ isLoading: true })).toBe('loading');
    expect(mapPublicShareState({})).toBe('loading');
  });

  it('拿到可下载的文件是 ready', () => {
    expect(mapPublicShareState({ data: makeView() })).toBe('ready');
  });

  it('文件存在但被禁止下载是 blocked（而不是 error）', () => {
    const blocked = makeView({ can_download: false, download_blocked_reason: '下载次数已用完' });
    expect(mapPublicShareState({ data: blocked })).toBe('blocked');
  });

  it('文件夹即使不能下载也照常展示基本信息', () => {
    const folder = makeView({ resource_type: 'folder', can_download: false, size_bytes: 0 });
    expect(mapPublicShareState({ data: folder })).toBe('ready');
  });

  it('请求失败是 error，且优先于加载态', () => {
    expect(
      mapPublicShareState({
        isLoading: true,
        isError: true,
        error: new ApiError('RESOURCE_NOT_FOUND', '不存在', ''),
      }),
    ).toBe('error');
  });

  it('有旧数据时刷新不回到 loading', () => {
    expect(mapPublicShareState({ isLoading: true, data: makeView() })).toBe('ready');
  });
});

describe('publicShareErrorMessage', () => {
  it('查不到链接时把三种可能都讲清楚（不存在 / 过期 / 撤销）', () => {
    const text = publicShareErrorMessage(new ApiError('RESOURCE_NOT_FOUND', 'x', ''));
    expect(text).toContain('不存在');
    expect(text).toContain('过期');
    expect(text).toContain('撤销');
  });

  it('限流与断网各有专门文案', () => {
    expect(publicShareErrorMessage(new ApiError('RATE_LIMITED', 'x', ''))).toContain('频繁');
    expect(publicShareErrorMessage(new ApiError('NETWORK_ERROR', 'x', ''))).toContain('网络');
  });

  it('其它错误码直接用后端文案（错误码是封闭集合，前端不自造分类）', () => {
    expect(publicShareErrorMessage(new ApiError('FORBIDDEN', '需要密码', ''))).toBe('需要密码');
    // 后端没给文案时退回兜底，不显示空字符串
    expect(publicShareErrorMessage(new ApiError('INTERNAL_ERROR', '', ''))).toBe(
      '无法打开这个分享链接',
    );
  });

  it('没有可用信息时给兜底文案，且不暴露内部细节', () => {
    expect(publicShareErrorMessage(undefined)).toBe('无法打开这个分享链接');
    expect(publicShareErrorMessage({ odd: true })).toBe('无法打开这个分享链接');
    expect(publicShareErrorMessage(new Error(''))).toBe('无法打开这个分享链接');
  });
});

describe('下载次数与有效期文案', () => {
  it('不设上限时说明不限次数', () => {
    expect(downloadQuotaLabel(makeView({ download_count: 3 }))).toBe('已下载 3 次（不限次数）');
  });

  it('设了上限时算出剩余次数', () => {
    expect(downloadQuotaLabel(makeView({ download_count: 2, max_downloads: 5 }))).toBe(
      '已下载 2 / 5 次，剩余 3 次',
    );
  });

  it('次数用完后剩余显示 0，不会出现负数', () => {
    expect(downloadQuotaLabel(makeView({ download_count: 5, max_downloads: 5 }))).toContain(
      '剩余 0 次',
    );
  });

  it('有效期文案覆盖长期有效、已过期与具体时间', () => {
    expect(expiryLabel(makeView())).toBe('长期有效');
    expect(expiryLabel(makeView({ expires_at: '2000-01-01T00:00:00Z' }))).toBe('已过期');
    expect(expiryLabel(makeView({ expires_at: '不是时间' }))).toBe('有效期未知');
    expect(expiryLabel(makeView({ expires_at: '2999-01-01T00:00:00Z' }))).toContain('到期时间');
  });
});

describe('publicDownloadUrl', () => {
  it('指向公开下载接口，并对 token 做编码', () => {
    expect(publicDownloadUrl('abc123')).toBe('/api/v1/public/shares/abc123/download');
    // token 里出现特殊字符时不能拼出坏 URL
    expect(publicDownloadUrl('a/b?c')).toBe('/api/v1/public/shares/a%2Fb%3Fc/download');
  });
});
