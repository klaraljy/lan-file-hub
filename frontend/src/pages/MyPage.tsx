/**
 * 「我的」页（AGENTS.md §15 阶段 ②）：手机底部 Tab 的第三个入口。
 *
 * 把「最近文件 / 回收站 / 设置 / 系统管理 / 退出登录」收在这里，
 * 一行一个、字号大、整行可点（≥52px），顶栏因此可以彻底精简掉这些入口。
 *
 * 2026-10-03 新增「配套手机应用」卡片：用户要一个**永远指向最新 APK**的下载入口
 * （"改完就更新的"）——版本/大小/构建时间都来自后端现读的安装包文件，
 * 重新跑一次 `scripts\build-android.ps1` 之后刷新页面就是新数字。
 */
import { useQuery } from '@tanstack/react-query';
import { Button, Card, Divider, List, Space, Typography, message } from 'antd';
import {
  AndroidOutlined,
  ClockCircleOutlined,
  DeleteOutlined,
  DownloadOutlined,
  LogoutOutlined,
  QrcodeOutlined,
  RightOutlined,
  SearchOutlined,
  SettingOutlined,
  TeamOutlined,
} from '@ant-design/icons';
import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import type { ReactNode } from 'react';

import { ResponsiveSheet } from '../components/ResponsiveSheet';
import { ShareLinkBox } from '../components/ShareLinkBox';
import { useAuth } from '../features/auth/AuthContext';
import { useIsMobile } from '../hooks/useIsMobile';
import { appDownloadUrl, describeAppRelease, fetchAppRelease } from '../services/appReleaseApi';

interface Entry {
  key: string;
  label: string;
  hint: string;
  icon: ReactNode;
}

/**
 * 「配套手机应用」卡片：应用名 + 版本/大小/时间 + 下载按钮 + 二维码。
 *
 * 为什么信息要问后端：APK 每次重新构建都会变（大小、时间、版本），
 * 写死在前端就会"改完不更新"（用户明确要求"改完就更新的"）。
 */
function PhoneAppCard() {
  const isMobile = useIsMobile();
  const [showQr, setShowQr] = useState(false);
  const release = useQuery({
    queryKey: ['app-release'],
    queryFn: fetchAppRelease,
    // 每次进「我的」都重新问一次：重新构建之后刷新页面就能看到新的数字
    staleTime: 0,
    refetchOnMount: 'always',
    retry: false,
  });

  const info = release.data;
  const download = appDownloadUrl(info ?? { download_path: '' });
  // 正在拉取时别急着说"没有安装包"：那会让人以为服务器上真没有（拉取通常只有几十毫秒）
  const infoText = info?.available
    ? describeAppRelease(info)
    : release.isLoading
      ? '正在读取安装包信息…'
      : '这台服务器上还没有准备好安装包，请联系管理员';

  async function copyDownloadUrl(url: string) {
    try {
      await navigator.clipboard.writeText(url);
      message.success('已复制下载地址');
    } catch {
      message.warning('浏览器不允许自动复制，请手动选中复制');
    }
  }

  return (
    <div style={{ padding: '4px 16px 12px' }}>
      <Space align="start" size={12} style={{ width: '100%' }}>
        <span style={{ fontSize: 22, color: '#389e0d', lineHeight: '28px' }}>
          <AndroidOutlined />
        </span>
        <div style={{ flex: 1, minWidth: 0 }}>
          <Typography.Text strong style={{ fontSize: 16 }}>
            配套手机应用（安卓）
          </Typography.Text>
          <div>
            <Typography.Text type="secondary" style={{ fontSize: 12 }}>
              {infoText}
            </Typography.Text>
          </div>
        </div>
      </Space>

      {info?.available && (
        <Space direction="vertical" size={8} style={{ width: '100%', marginTop: 10 }}>
          <Space wrap>
            <Button type="primary" size="large" icon={<DownloadOutlined />} href={download.url}>
              下载安装
            </Button>
            {!isMobile && (
              <Button size="large" icon={<QrcodeOutlined />} onClick={() => setShowQr(true)}>
                用手机扫码安装
              </Button>
            )}
          </Space>
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            {isMobile
              ? '点「下载安装」就下到这台手机；安装时选择允许「安装未知应用」即可。'
              : '电脑上直接点会把安装包下到电脑；要装到手机上，点「用手机扫码安装」扫一下最快。'}
          </Typography.Text>
        </Space>
      )}

      <ResponsiveSheet
        open={showQr}
        onClose={() => setShowQr(false)}
        title="用手机装这个应用"
        desktopWidth={520}
      >
        <Typography.Paragraph type="secondary">
          用手机相机或微信「扫一扫」下面这个二维码，手机会直接开始下载安装包；
          下完点开，允许「安装未知应用」就能装上。装好后在 App 里填这台电脑的地址即可连上。
        </Typography.Paragraph>
        <ShareLinkBox
          url={download.url}
          warning={download.warning}
          notice="这个地址指向服务器上最新的安装包，管理员重新构建后它自动就是新的。"
          onCopy={(url) => void copyDownloadUrl(url)}
        />
      </ResponsiveSheet>
    </div>
  );
}

export function MyPage() {
  const { user, logout } = useAuth();
  const navigate = useNavigate();

  const entries: Entry[] = [
    {
      key: '/search',
      label: '搜索',
      hint: '按名字找文件或文件夹（所有文件夹一起搜）',
      icon: <SearchOutlined />,
    },
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

      <Divider style={{ margin: 0 }} />
      <PhoneAppCard />
      <Divider style={{ margin: 0 }} />

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
