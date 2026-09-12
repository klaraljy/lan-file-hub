/**
 * 应用外壳（docs/frontend.md §2.2）。
 *
 * 职责：左侧导航（我的文件 / 最近文件 / 分享给我的 / 回收站 / 设置）+ 顶部用户信息条，
 * 中间用 <Outlet/> 渲染具体页面。
 *
 * 页面自己不再画标题栏与退出按钮——那些属于外壳，避免每个页面各写一套。
 * 导航高亮规则在 app/navigation.ts 里（纯函数，可单测）。
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
import type { ReactNode } from 'react';

import { navItemsForRole, selectedNavKey, type NavItem } from './navigation';
import { useAuth } from '../features/auth/AuthContext';

const { Header, Sider, Content } = Layout;

const ICONS: Record<NavItem['icon'], ReactNode> = {
  folder: <FolderOutlined />,
  clock: <ClockCircleOutlined />,
  share: <ShareAltOutlined />,
  delete: <DeleteOutlined />,
  setting: <SettingOutlined />,
  admin: <TeamOutlined />,
};

export function AppLayout() {
  const { user, logout } = useAuth();
  const navigate = useNavigate();
  const location = useLocation();

  async function handleLogout() {
    await logout();
    navigate('/login', { replace: true });
  }

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Sider breakpoint="lg" collapsedWidth={64} theme="light" width={200}>
        <div
          style={{
            height: 56,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            fontWeight: 600,
          }}
        >
          lan-file-hub
        </div>
        <Menu
          mode="inline"
          selectedKeys={[selectedNavKey(location.pathname)]}
          items={navItemsForRole(user?.role).map((item) => ({
            key: item.key,
            icon: ICONS[item.icon],
            label: <Link to={item.key}>{item.label}</Link>,
          }))}
        />
      </Sider>

      <Layout>
        <Header
          style={{
            background: '#fff',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'flex-end',
            paddingInline: 24,
            gap: 12,
          }}
        >
          <Space>
            <Typography.Text type="secondary">{user?.username ?? '未登录'}</Typography.Text>
            {user && (
              <Tag color={user.role === 'ADMIN' ? 'gold' : 'blue'}>
                {user.role === 'ADMIN' ? '管理员' : '普通用户'}
              </Tag>
            )}
            <Typography.Link onClick={() => void handleLogout()}>退出登录</Typography.Link>
          </Space>
        </Header>

        <Content style={{ padding: 24 }}>
          <div style={{ maxWidth: 1200, margin: '0 auto' }}>
            <Outlet />
          </div>
        </Content>
      </Layout>
    </Layout>
  );
}
