import { describe, expect, it } from 'vitest';

import {
  APP_UA_MARKER,
  appUpdateState,
  installedBuildFromUserAgent,
} from '../src/features/app/appUpdate';

/**
 * 「在 App 里就把下载说成更新」（用户 2026-10-03 要求）。
 *
 * 这里钉的是"怎么认出自己在 App 里、该显示哪个按钮"：
 * 认错了要么让已经装了的人再点一次"下载安装"，要么显示"已是最新"却其实有新版。
 */
const IN_APP_UA =
  'Mozilla/5.0 (Linux; Android 13; V2254A Build/TP1A.220624.014) AppleWebKit/537.36 ' +
  '(KHTML, like Gecko) Version/4.0 Chrome/120.0.0.0 Mobile Safari/537.36 LanFileHubApp/4';

describe('installedBuildFromUserAgent', () => {
  it('认得出 App 内打开，并读出构建号', () => {
    expect(installedBuildFromUserAgent(IN_APP_UA)).toBe(4);
  });

  it('普通浏览器（手机 / 电脑）返回 null', () => {
    expect(
      installedBuildFromUserAgent(
        'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/120.0 Safari/537.36',
      ),
    ).toBeNull();
    expect(
      installedBuildFromUserAgent(
        'Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/120.0 Mobile Safari/537.36',
      ),
    ).toBeNull();
  });

  it('空值、只有标记没数字、数字为 0 都不算', () => {
    expect(installedBuildFromUserAgent('')).toBeNull();
    expect(installedBuildFromUserAgent(undefined)).toBeNull();
    expect(installedBuildFromUserAgent('Chrome/120 LanFileHubApp/')).toBeNull();
    expect(installedBuildFromUserAgent('Chrome/120 LanFileHubApp/0')).toBeNull();
  });

  it('标记后面还有别的内容也照样读得出数字', () => {
    expect(installedBuildFromUserAgent(`${APP_UA_MARKER}12 Extra/1`)).toBe(12);
  });
});

describe('appUpdateState', () => {
  it('不在 App 里 → 下载安装', () => {
    expect(appUpdateState(null, 7)).toBe('install');
  });

  it('服务器上有更新的构建号 → 更新', () => {
    expect(appUpdateState(3, 4)).toBe('update');
    expect(appUpdateState(1, 99)).toBe('update');
  });

  it('已经是最新（或服务器上的更旧）→ 已是最新', () => {
    expect(appUpdateState(4, 4)).toBe('latest');
    expect(appUpdateState(9, 4)).toBe('latest');
  });

  it('服务器上读不到构建号时退回"下载安装"，不瞎说已是最新', () => {
    expect(appUpdateState(4, undefined)).toBe('install');
    expect(appUpdateState(4, 0)).toBe('install');
  });
});
