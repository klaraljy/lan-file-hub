/**
 * 手机底部 Tab 栏（AGENTS.md §15 阶段 ②：手机要像一个 App）。
 *
 * 手机上导航的**主入口**就是这里，不再用汉堡菜单（用户反馈"憋屈"的主要来源之一）。
 * 每个 Tab 的点击区域是一整条 1/3 宽、56px 高的按钮，手指点得准。
 *
 * ⚠️ 第 4 个位置留给以后的「广场」（阶段 ⑤）：在 `navigation.ts` 的 `MOBILE_TABS`
 * 里加一项即可，这里会自动均分宽度，不需要改布局。
 */
import type { ReactNode } from 'react';
import {
  FolderOutlined,
  ShareAltOutlined,
  TeamOutlined,
  UserOutlined,
} from '@ant-design/icons';
import { useLocation, useNavigate } from 'react-router-dom';

import { MOBILE_TABS, mobileTabForPath, type MobileTab } from './navigation';

const ICONS: Record<MobileTab['icon'], ReactNode> = {
  folder: <FolderOutlined />,
  plaza: <TeamOutlined />,
  share: <ShareAltOutlined />,
  user: <UserOutlined />,
};

/** 底栏高度：内容区按它留内边距，否则最后一条会被挡住 */
export const TAB_BAR_HEIGHT = 56;

export function MobileTabBar() {
  const navigate = useNavigate();
  const location = useLocation();
  const active = mobileTabForPath(location.pathname);

  return (
    <nav
      aria-label="主导航"
      style={{
        position: 'fixed',
        left: 0,
        right: 0,
        bottom: 0,
        zIndex: 100,
        display: 'flex',
        background: '#fff',
        borderTop: '1px solid #f0f0f0',
        boxShadow: '0 -2px 8px rgba(0, 0, 0, 0.04)',
        // 刘海屏底部安全区：不加会被系统手势条压住
        paddingBottom: 'env(safe-area-inset-bottom)',
      }}
    >
      {MOBILE_TABS.map((tab) => {
        const selected = tab.key === active;
        return (
          <button
            key={tab.key}
            type="button"
            // 显式给可访问名：图标自带 aria-label（如 "folder"），不写这个读屏和测试都会念成 "folder 文件"
            aria-label={tab.label}
            aria-current={selected ? 'page' : undefined}
            onClick={() => navigate(tab.key)}
            style={{
              flex: 1,
              height: TAB_BAR_HEIGHT,
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'center',
              justifyContent: 'center',
              gap: 3,
              border: 'none',
              background: 'transparent',
              cursor: 'pointer',
              color: selected ? '#1677ff' : '#8c8c8c',
              fontSize: 12,
              padding: 0,
            }}
          >
            <span style={{ fontSize: 20, lineHeight: 1 }}>{ICONS[tab.icon]}</span>
            <span>{tab.label}</span>
          </button>
        );
      })}
    </nav>
  );
}
