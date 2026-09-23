/**
 * 左侧导航的定义与高亮计算（docs/frontend.md §2.2 的五个入口）。
 *
 * selectedNavKey 是纯函数：给一个 pathname 返回应该高亮的菜单 key，
 * 这样「进了 /files/xxx 时「我的文件」仍然高亮」这种规则可以被单测钉住。
 */

export interface NavItem {
  key: string;
  label: string;
  /** 菜单项对应的图标名，由 AppLayout 映射成组件（这里不引入 React，保持纯数据） */
  icon: 'folder' | 'clock' | 'share' | 'delete' | 'setting' | 'admin';
}

export const MAIN_NAV_ITEMS: readonly NavItem[] = [
  { key: '/files', label: '我的文件', icon: 'folder' },
  { key: '/recent', label: '最近文件', icon: 'clock' },
  { key: '/shared', label: '分享给我的', icon: 'share' },
  { key: '/trash', label: '回收站', icon: 'delete' },
  { key: '/settings', label: '设置', icon: 'setting' },
];

/** 只有管理员可见的入口（阶段十三）。前端隐藏只是界面问题，权限判定在后端 */
export const ADMIN_NAV_ITEM: NavItem = { key: '/admin', label: '系统管理', icon: 'admin' };

/** 全部入口（含管理项）：高亮计算用它，这样 /admin 也能正确高亮 */
const ALL_NAV_ITEMS: readonly NavItem[] = [...MAIN_NAV_ITEMS, ADMIN_NAV_ITEM];

/** 按角色给出可见菜单：普通用户看不到管理入口 */
export function navItemsForRole(role: string | undefined): readonly NavItem[] {
  return role === 'ADMIN' ? [...MAIN_NAV_ITEMS, ADMIN_NAV_ITEM] : MAIN_NAV_ITEMS;
}

/**
 * 根据当前路径判断应该高亮哪个菜单项。
 * 目录详情页（/files/:folderId）也属于「我的文件」；未知路径不高亮任何项。
 */
export function selectedNavKey(pathname: string): string {
  const path = pathname.split('?')[0] ?? '';
  // 前缀匹配必须带上斜杠边界，避免 /files-old 被当成 /files
  for (const item of ALL_NAV_ITEMS) {
    if (path === item.key || path.startsWith(`${item.key}/`)) {
      return item.key;
    }
  }
  return '';
}

/** 公开分享页不需要应用外壳（无导航、无登录） */
export function isPublicPath(pathname: string): boolean {
  return pathname === '/s' || pathname.startsWith('/s/');
}

/* ------------------------------------------------------------------ *
 * 手机底部 Tab（AGENTS.md §15 阶段 ②：手机要像 App，而不是压扁的桌面版）
 * ------------------------------------------------------------------ */

export interface MobileTab {
  /** 点这个 Tab 去哪个路径 */
  key: string;
  label: string;
  icon: 'folder' | 'share' | 'user';
  /** 哪些路径算在这个 Tab 下（用于高亮）；前缀匹配带斜杠边界 */
  matches: readonly string[];
}

/**
 * 底部 Tab 定义（手机上导航的**主入口**）。
 *
 * 「最近文件 / 回收站 / 设置 / 系统管理 / 退出登录」都收进「我的」页，
 * 顶栏因此不再需要汉堡菜单——这也是用户反馈"憋屈"的主要来源。
 *
 * ⚠️ 第 4 个位置**留给以后的「广场」**（AGENTS.md §15 阶段 ⑤）：
 * 届时在下面这个数组里追加一项即可，布局不用改（Tab 栏自动均分）。
 */
export const MOBILE_TABS: readonly MobileTab[] = [
  { key: '/files', label: '文件', icon: 'folder', matches: ['/files'] },
  { key: '/shared', label: '分享给我的', icon: 'share', matches: ['/shared'] },
  {
    key: '/me',
    label: '我的',
    icon: 'user',
    matches: ['/me', '/recent', '/trash', '/settings', '/admin'],
  },
];

/** 当前路径应该高亮哪个底部 Tab（纯函数，可单测） */
export function mobileTabForPath(pathname: string): string {
  const path = pathname.split('?')[0] ?? '';
  for (const tab of MOBILE_TABS) {
    for (const prefix of tab.matches) {
      if (path === prefix || path.startsWith(`${prefix}/`)) {
        return tab.key;
      }
    }
  }
  // 未知路径（例如以后新增的页面）落在第一个 Tab，避免底部栏全部熄灭
  return MOBILE_TABS[0]?.key ?? '/files';
}
