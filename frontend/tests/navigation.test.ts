import { describe, expect, it } from 'vitest';

import {
  ADMIN_NAV_ITEM,
  MAIN_NAV_ITEMS,
  MOBILE_TABS,
  isPublicPath,
  mobileTabForPath,
  navItemsForRole,
  selectedNavKey,
} from '../src/app/navigation';

/**
 * 左侧导航是六个入口（阶段 ⑤ 加了「广场」），高亮规则必须稳定：
 * 目录详情页也要算「我的文件」，前缀相近的路径不能被误判。
 */
describe('selectedNavKey', () => {
  it('六个入口本身都能高亮自己', () => {
    for (const item of MAIN_NAV_ITEMS) {
      expect(selectedNavKey(item.key)).toBe(item.key);
    }
    expect(MAIN_NAV_ITEMS.map((item) => item.key)).toEqual([
      '/files',
      '/plaza',
      '/recent',
      '/shared',
      '/trash',
      '/settings',
    ]);
  });

  it('进入子目录时仍然是「我的文件」高亮', () => {
    expect(selectedNavKey('/files/8f1c-2b')).toBe('/files');
    expect(selectedNavKey('/files/8f1c-2b/anything')).toBe('/files');
  });

  it('前缀相近的路径不会被误判', () => {
    expect(selectedNavKey('/files-old')).toBe('');
    expect(selectedNavKey('/trashy')).toBe('');
  });

  it('带查询串也能正确判断', () => {
    expect(selectedNavKey('/trash?page=2')).toBe('/trash');
  });

  it('未知路径与公开分享页不高亮任何菜单项', () => {
    expect(selectedNavKey('/')).toBe('');
    expect(selectedNavKey('/s/some-token')).toBe('');
  });
});

describe('isPublicPath', () => {
  it('只有 /s 与 /s/:token 算公开页', () => {
    expect(isPublicPath('/s/abc')).toBe(true);
    expect(isPublicPath('/s')).toBe(true);
    expect(isPublicPath('/files')).toBe(false);
    // 不能把 /settings 之类误判成公开页
    expect(isPublicPath('/settings')).toBe(false);
    expect(isPublicPath('/share/abc')).toBe(false);
  });
});
/**
 * 管理入口（阶段十三）：只有管理员能看到菜单项。
 * 前端隐藏只是界面问题——权限由后端判定，这里只钉住"普通用户看不到入口"。
 */
describe('navItemsForRole', () => {
  it('管理员多一个「系统管理」入口', () => {
    const items = navItemsForRole('ADMIN');
    expect(items.map((item) => item.key)).toEqual([
      '/files',
      '/plaza',
      '/recent',
      '/shared',
      '/trash',
      '/settings',
      '/admin',
    ]);
  });

  it('普通用户与未登录都不显示管理入口', () => {
    for (const role of ['USER', undefined, '']) {
      const keys = navItemsForRole(role).map((item) => item.key);
      expect(keys).not.toContain('/admin');
    }
  });

  it('管理入口自己能被高亮，且不污染其他入口', () => {
    expect(selectedNavKey(ADMIN_NAV_ITEM.key)).toBe('/admin');
    expect(selectedNavKey('/admin/users')).toBe('/admin');
  });
});

/**
 * 手机底部 Tab（AGENTS.md §15 阶段 ②/⑤）：手机上导航的主入口。
 * 「最近文件 / 回收站 / 设置 / 系统管理」都算在「我的」Tab 下；
 * 阶段 ⑤ 起多了第 2 个 Tab「广场」（谁在线、直接发文件给他）。
 */
describe('mobileTabForPath', () => {
  it('四个 Tab 各自能高亮自己', () => {
    expect(MOBILE_TABS.map((tab) => tab.key)).toEqual(['/files', '/plaza', '/shared', '/me']);
    for (const tab of MOBILE_TABS) {
      expect(mobileTabForPath(tab.key)).toBe(tab.key);
    }
  });

  it('广场 Tab 有自己的路径，不会被当成「文件」', () => {
    expect(mobileTabForPath('/plaza')).toBe('/plaza');
    expect(mobileTabForPath('/plaza?tab=1')).toBe('/plaza');
  });

  it('目录详情页仍算「文件」', () => {
    expect(mobileTabForPath('/files/8f1c-2b')).toBe('/files');
  });

  it('最近文件 / 回收站 / 设置 / 系统管理都归到「我的」', () => {
    for (const path of ['/recent', '/trash', '/settings', '/admin', '/admin/users']) {
      expect(mobileTabForPath(path)).toBe('/me');
    }
  });

  it('前缀相近的路径不会被误判', () => {
    // /trashy 不是 /trash，按"未知路径"处理（落在第一个 Tab），而不是误判成「我的」
    expect(mobileTabForPath('/trashy')).toBe('/files');
    expect(mobileTabForPath('/files-old')).toBe('/files');
  });

  it('带查询串、以及完全未知的路径都不会让底栏熄灭', () => {
    expect(mobileTabForPath('/shared?page=2')).toBe('/shared');
    expect(mobileTabForPath('/')).toBe('/files');
    expect(mobileTabForPath('/something-new')).toBe('/files');
  });
});