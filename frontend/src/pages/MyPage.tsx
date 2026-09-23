/**
 * 「我的」页（AGENTS.md §15 阶段 ②）：手机底部 Tab 的第三个入口。
 *
 * 把「最近文件 / 回收站 / 设置 / 系统管理 / 退出登录」收在这里，
 * 一行一个、字号大、整行可点（≥52px），顶栏因此可以彻底精简掉这些入口。
 */
import { Button, Card, List, Typography } from 'antd';
import {
  ClockCircleOutlined,
  DeleteOutlined,
  LogoutOutlined,
  RightOutlined,
  SettingOutlined,
  TeamOutlined,
} from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import type { ReactNode } from 'react';

import { useAuth } from '../features/auth/AuthContext';

interface Entry {
  key: string;
  label: string;
  hint: string;
  icon: ReactNode;
}

export function MyPage() {
  const { user, logout } = useAuth();
  const navigate = useNavigate();

  const entries: Entry[] = [
    {
      key: '/recent',
      label: '最近文件',
      hint: '最近打开或改过的文件',
      icon: <ClockCircleOutlined />,
    },
    {
      key: '/trash',
      label: '回收站',
      hint: '删掉的东西在这里，可以恢复',
      icon: <DeleteOutlined />,
    },
    {
      key: '/settings',
      label: '设置',
      hint: '账号、头像与偏好',
      icon: <SettingOutlined />,
    },
  ];
  if (user?.role === 'ADMIN') {
    entries.push({
      key: '/admin',
      label: '系统管理',
      hint: '用户、指标与审计日志',
      icon: <TeamOutlined />,
    });
  }

  async function handleLogout() {
    await logout();
    navigate('/login', { replace: true });
  }

  return (
    <Card styles={{ body: { padding: 0 } }}>
      <div style={{ padding: '16px 16px 12px' }}>
        <Typography.Title level={4} style={{ marginBottom: 2 }}>
          {user?.username ?? '未登录'}
        </Typography.Title>
        <Typography.Text type="secondary">
          {user?.role === 'ADMIN' ? '管理员' : '普通用户'}
        </Typography.Text>
      </div>

      <List
        dataSource={entries}
        renderItem={(item) => (
          <List.Item
            onClick={() => navigate(item.key)}
            style={{
              padding: '14px 16px',
              minHeight: 60,
              cursor: 'pointer',
              alignItems: 'center',
            }}
            extra={<RightOutlined style={{ color: '#bfbfbf' }} />}
          >
            <List.Item.Meta
              avatar={<span style={{ fontSize: 20, color: '#595959' }}>{item.icon}</span>}
              title={<span style={{ fontSize: 16 }}>{item.label}</span>}
              description={
                <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                  {item.hint}
                </Typography.Text>
              }
            />
          </List.Item>
        )}
      />

      <div style={{ padding: 16 }}>
        <Button
          block
          danger
          size="large"
          icon={<LogoutOutlined />}
          onClick={() => void handleLogout()}
        >
          退出登录
        </Button>
      </div>
    </Card>
  );
}
