import { describe, expect, it } from 'vitest';

import {
  EMPTY_APP_RELEASE,
  appDownloadUrl,
  describeAppRelease,
  formatBuildTime,
  formatSize,
  normalizeAppRelease,
} from '../src/services/appReleaseApi';
import { setServerLanAddress } from '../src/services/publicUrl';

/**
 * 「配套手机应用」卡片上的文字（用户 2026-10-03：大小/时间必须**跟着安装包走**）。
 *
 * 这里只钉纯函数：数字怎么变成人看得懂的字。
 * "重新构建之后数字会变"是后端的事（`backend/tests/unit/app_release_test.cpp`），
 * 前端这边只要保证**不写死、不显示 undefined / NaN**。
 */
describe('formatSize', () => {
  it('小于 1 MB 用 KB，大于等于 1 MB 用 MB', () => {
    expect(formatSize(347_000)).toBe('339 KB');
    expect(formatSize(2_621_440)).toBe('2.5 MB');
  });

  it('没有数据时返回空串（而不是 "0 KB" / "NaN"）', () => {
    expect(formatSize(0)).toBe('');
    expect(formatSize(Number.NaN)).toBe('');
    expect(formatSize(-1)).toBe('');
  });
});

describe('formatBuildTime', () => {
  it('把后端的 UTC 时间转成本地时间显示', () => {
    // 2026-10-03T09:01:18Z → 本机时区（中国 +8）= 17:01
    const text = formatBuildTime('2026-10-03T09:01:18Z');
    expect(text).toMatch(/^2026-10-0[34] \d{2}:\d{2}$/);
  });

  it('解析不了就原样显示，绝不显示 Invalid Date', () => {
    expect(formatBuildTime('昨天')).toBe('昨天');
    expect(formatBuildTime('')).toBe('');
  });
});

describe('describeAppRelease', () => {
  it('版本 / 大小 / 时间拼成一行；缺什么就不显示什么', () => {
    expect(
      describeAppRelease({
        ...EMPTY_APP_RELEASE,
        available: true,
        version_name: '1.0',
        size_bytes: 347_000,
        built_at: '2026-10-03T09:01:18Z',
      }),
    ).toMatch(/^版本 1\.0 · 339 KB · 2026-10-0[34] \d{2}:\d{2} 构建$/);

    // 只有大小（还没写侧车文件时就是这个样子）
    expect(
      describeAppRelease({ ...EMPTY_APP_RELEASE, available: true, size_bytes: 347_000 }),
    ).toBe('339 KB');
  });
});

describe('normalizeAppRelease', () => {
  it('后端少给字段也不会让界面出现 undefined', () => {
    const info = normalizeAppRelease({ available: true, size_bytes: 1024 });
    expect(info.size_bytes).toBe(1024);
    expect(info.download_path).toBe('/api/v1/app/download');
    expect(info.version_name).toBe('');
  });
});

describe('appDownloadUrl', () => {
  it('电脑上用 127.0.0.1 打开时换成局域网地址（否则手机扫了也下不到）', () => {
    setServerLanAddress('192.168.43.240');
    const result = appDownloadUrl({ download_path: '/api/v1/app/download' }, {
      origin: 'http://127.0.0.1:5173',
      hostname: '127.0.0.1',
    });
    expect(result.url).toBe('http://192.168.43.240:5173/api/v1/app/download');
    expect(result.warning).toContain('192.168.43.240');
    setServerLanAddress('');
  });

  it('本来就是局域网地址访问：原样使用，不给警告', () => {
    const result = appDownloadUrl(EMPTY_APP_RELEASE, {
      origin: 'http://192.168.43.240:9180',
      hostname: '192.168.43.240',
    });
    expect(result.url).toBe('http://192.168.43.240:9180/api/v1/app/download');
    expect(result.warning).toBeUndefined();
  });

  it('没有 download_path 时用默认路径，不会拼出 undefined', () => {
    expect(appDownloadUrl(undefined).url).toContain('/api/v1/app/download');
  });
});
