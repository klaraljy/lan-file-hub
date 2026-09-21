/**
 * 应用外壳（docs/frontend.md §2.2）。
 *
 * 职责：导航（我的文件 / 最近文件 / 分享给我的 / 回收站 / 设置）+ 顶部用户信息条，
 * 中间用 <Outlet/> 渲染具体页面。
 *
 * 手机适配（AGENTS.md §14.1：手机上难用 = 产品缺陷）：
 *   - 窄屏（< lg / 992px）不再渲染左侧栏，导航收进 **抽屉**，由顶部的**汉堡按钮**打开；
 *   - 点某个导航项后**自动关闭**抽屉，不用手动再点一次；
 *   - 顶部用户区精简为「用户名 + 角色标签 + 退出登录」，用户名过长省略号，不撑爆一行。
 *
 * 页面自己不再画标题栏与退出按钮——那些属于外壳，避免每个页面各写一套。
 * 导航高亮规则在 app/navigation.ts 里（纯函数，可单测）。
 */
import { useEffect, useState } from 'react';
import { Button, Drawer, Layout, Menu, Space, Tag, Typography } from 'antd';
import {
  ClockCircleOutlined,
  DeleteOutlined,
  FolderOutlined,
  MenuOutlined,
  SettingOutlined,
  ShareAltOutlined,
  TeamOutlined,
} from '@ant-design/icons';
import { Link, Outlet, useLocation, useNavigate } from 'react-router-dom';
import type { ReactNode } from 'react';

import { navItemsForRole, selectedNavKey, type NavItem } from './navigation';
import { useAuth } from '../features/auth/AuthContext';
import { TOUCH_HEIGHT, useIsMobile } from '../hooks/useIsMobile';

const { Header, Sider, Content } = Layout;

const ICONS: Record<NavItem['icon'], ReactNode> = {
  folder: <FolderOutlined />,
  clock: <ClockCircleOutlined />,
  share: <ShareAltOutlined />,
  delete: <DeleteOutlined />,
  setting: <SettingOutlined />,
  admin: <TeamOutlined />,
};

const BRAND = 'lan-file-hub';

export function AppLayout() {
  const { user, logout } = useAuth();
  const navigate = useNavigate();
  const location = useLocation();
  const isMobile = useIsMobile();
  const [navOpen, setNavOpen] = useState(false);

  // 手机：切换路由后自动收起抽屉（点了导航项就该看到内容，而不是还要再点一次关闭）
  useEffect(() => {
    setNavOpen(false);
  }, [location.pathname]);

  async function handleLogout() {
    await logout();
    navigate('/login', { replace: true });
  }

  const menu = (
    <Menu
      mode="inline"
      selectedKeys={[selectedNavKey(location.pathname)]}
      // 手机上点完就关；桌面端 Sider 常驻，这个回调没有副作用
      onClick={() => setNavOpen(false)}
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
              fontWeight: 600,
            }}
          >
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
            // 手机上左边放汉堡按钮，右边是用户信息
            justifyContent: isMobile ? 'space-between' : 'flex-end',
            paddingInline: isMobile ? 12 : 24,
            gap: 8,
          }}
        >
          {isMobile && (
            <Space size={4}>
              <Button
                type="text"
                aria-label="打开导航菜单"
                icon={<MenuOutlined style={{ fontSize: 18 }} />}
                style={{ height: TOUCH_HEIGHT, minWidth: TOUCH_HEIGHT }}
                onClick={() => setNavOpen(true)}
              />
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
                maxWidth: isMobile ? 88 : 200,
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
            <Typography.Link onClick={() => void handleLogout()} style={{ whiteSpace: 'nowrap' }}>
              退出登录
            </Typography.Link>
          </Space>
        </Header>

        <Content style={{ padding: isMobile ? 12 : 24 }}>
          <div style={{ maxWidth: 1200, margin: '0 auto' }}>
            <Outlet />
          </div>
        </Content>
      </Layout>

      {/* 手机导航抽屉：同一份菜单，避免两处维护 */}
      {isMobile && (
        <Drawer
          open={navOpen}
          placement="left"
          width={240}
          title={BRAND}
          onClose={() => setNavOpen(false)}
          styles={{ body: { padding: 0 } }}
          destroyOnClose
        >
          {menu}
        </Drawer>
      )}
    </Layout>
  );
}
