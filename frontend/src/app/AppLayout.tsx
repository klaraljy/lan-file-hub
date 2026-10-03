/**
 * 应用外壳（docs/frontend.md §2.2）。
 *
 * 桌面端（≥ lg / 992px）：左侧导航常驻 + 顶栏用户信息 —— 与改造前一致，功能不变。
 *
 * 手机端（< lg）：导航主入口是**底部 Tab 栏**（AGENTS.md §15 阶段 ②，用户要求"像一个 App"）：
 *   - 顶栏只留品牌与当前用户，不再用汉堡菜单当主入口（那是"憋屈"的主要来源）；
 *   - 「最近文件 / 回收站 / 设置 / 系统管理 / 退出登录」都收进「我的」页；
 *   - 内容区底部按 Tab 栏高度留白，最后一条不会被挡住。
 *
 * 页面自己不再画标题栏与退出按钮——那些属于外壳，避免每个页面各写一套。
 * 导航高亮规则在 app/navigation.ts 里（纯函数，可单测；底部 Tab 的高亮用 mobileTabForPath）。
 */
import { Layout, Menu, Space, Tag, Typography } from 'antd';
import {
  ClockCircleOutlined,
  DeleteOutlined,
  FolderOutlined,
  SettingOutlined,
  ShareAltOutlined,
  TeamOutlined,
} from '@ant-design/icons';
import { Link, Outlet, useLocation, useNavigate } from 'react-router-dom';
import { useEffect, type ReactNode } from 'react';

import { MobileTabBar, TAB_BAR_HEIGHT } from './MobileTabBar';
import { navItemsForRole, selectedNavKey, type NavItem } from './navigation';
import { useAuth } from '../features/auth/AuthContext';
import { usePresenceHeartbeat } from '../features/presence/usePresence';
import { useIsMobile } from '../hooks/useIsMobile';
import { startServerLanRefresh } from '../services/networkApi';

const { Header, Sider, Content } = Layout;

const ICONS: Record<NavItem['icon'], ReactNode> = {
  folder: <FolderOutlined />,
  clock: <ClockCircleOutlined />,
  share: <ShareAltOutlined />,
  delete: <DeleteOutlined />,
  setting: <SettingOutlined />,
  admin: <TeamOutlined />,
  plaza: <TeamOutlined />,
};

const BRAND = 'lan-file-hub';

/**
 * 品牌图标（用户 2026-10-02 要求放在左上角）：源头 `frontend/public/logo.png`，
 * 与浏览器标签图标 `favicon.ico`、桌面快捷方式图标是同一套绿文件夹图案。
 * 尺寸给固定值并留圆角，避免不同 DPI 下忽大忽小。
 */
function BrandLogo({ size }: { size: number }) {
  return (
    <img
      src="/logo.png"
      alt="lan-file-hub"
      width={size}
      height={size}
      style={{ borderRadius: Math.round(size / 5), display: 'block', flex: 'none' }}
    />
  );
}

export function AppLayout() {
  const { user, logout } = useAuth();
  const navigate = useNavigate();
  const location = useLocation();
  const isMobile = useIsMobile();
  // 在线状态心跳：登录后自动开始（每 30 秒），登出后自动停（阶段 ⑤）
  usePresenceHeartbeat();
  // 当前局域网地址：打开 App 拉一次，之后定时/窗口聚焦/网络恢复时刷新。
  // 用户换网络（校园网 ↔ 手机热点）后，分享链接要跟着变，不能停在打开页面时的旧地址。
  useEffect(() => {
    startServerLanRefresh();
  }, []);

  async function handleLogout() {
    await logout();
    navigate('/login', { replace: true });
  }

  const menu = (
    <Menu
      mode="inline"
      selectedKeys={[selectedNavKey(location.pathname)]}
      items={navItemsForRole(user?.role).map((item) => ({
        key: item.key,
        icon: ICONS[item.icon],
        label: <Link to={item.key}>{item.label}</Link>,
      }))}
    />
  );

  return (
    <Layout style={{ minHeight: '100vh' }}>
      {!isMobile && (
        <Sider breakpoint="lg" collapsedWidth={64} theme="light" width={200}>
          <div
            style={{
              height: 56,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              gap: 8,
              fontWeight: 600,
            }}
          >
            <BrandLogo size={26} />
            {BRAND}
          </div>
          {menu}
        </Sider>
      )}

      <Layout>
        <Header
          style={{
            background: '#fff',
            display: 'flex',
            alignItems: 'center',
            justifyContent: isMobile ? 'space-between' : 'flex-end',
            paddingInline: isMobile ? 12 : 24,
            gap: 8,
            // 手机顶栏收窄一点，把高度还给内容
            height: isMobile ? 52 : undefined,
            lineHeight: 'normal',
          }}
        >
          {isMobile && (
            <Space size={8} align="center">
              <BrandLogo size={24} />
              <Typography.Text strong>{BRAND}</Typography.Text>
            </Space>
          )}

          {/* lineHeight 归零：否则整组继承 AntD Header 的 64px 行高，
              加上 ellipsis 自带的 vertical-align:bottom，用户名会被顶到顶栏底部（实测踩到） */}
          <Space size={isMobile ? 6 : 12} style={{ minWidth: 0, lineHeight: 'normal' }}>
            <Typography.Text
              type="secondary"
              ellipsis
              style={{
                maxWidth: isMobile ? 120 : 200,
                lineHeight: 'normal',
                verticalAlign: 'middle',
              }}
            >
              {user?.username ?? '未登录'}
            </Typography.Text>
            {user && (
              <Tag color={user.role === 'ADMIN' ? 'gold' : 'blue'} style={{ marginInlineEnd: 0 }}>
                {user.role === 'ADMIN' ? '管理员' : '普通用户'}
              </Tag>
            )}
            {/* 桌面端保留顶栏退出；手机端退出登录在「我的」页里，不占顶栏 */}
            {!isMobile && (
              <Typography.Link onClick={() => void handleLogout()} style={{ whiteSpace: 'nowrap' }}>
                退出登录
              </Typography.Link>
            )}
          </Space>
        </Header>

        <Content
          style={{
            padding: isMobile ? 12 : 24,
            // 底部 Tab 栏是 fixed 的，内容要留出它的高度
            paddingBottom: isMobile ? TAB_BAR_HEIGHT + 20 : 24,
          }}
        >
          <div style={{ maxWidth: 1200, margin: '0 auto' }}>
            <Outlet />
          </div>
        </Content>
      </Layout>

      {isMobile && <MobileTabBar />}
    </Layout>
  );
}
