/**
 * 「配套手机应用」下载块（2026-10-03）。
 *
 * 用户 2026-10-03 反馈两点，所以做成一个**可复用**组件、两种形态：
 *   ① 「我的」页里原来那张卡片**太大**了 → `variant="compact"`：一行就够（名称 + 版本/构建号/大小/时间
 *      + 下载按钮 + 二维码入口）；
 *   ② 电脑上也要有下载位置 → `variant="full"`：放在「设置」页，写清"怎么装到手机上"。
 *
 * 版本 / 构建号 / 大小 / 构建时间**都来自后端现读的 APK 文件**（见 appReleaseApi.ts）：
 * 重新构建一次、刷新页面就是新数字，不需要重启后端。
 */
import { useState } from 'react';
import { App as AntApp, Button, Space, Typography } from 'antd';
import { AndroidOutlined, DownloadOutlined, QrcodeOutlined } from '@ant-design/icons';
import { useQuery } from '@tanstack/react-query';

import { ResponsiveSheet } from './ResponsiveSheet';
import { ShareLinkBox } from './ShareLinkBox';
import { useIsMobile } from '../hooks/useIsMobile';
import { appDownloadUrl, describeAppRelease, fetchAppRelease } from '../services/appReleaseApi';

interface Props {
  /** compact = 「我的」页里的一行；full = 「设置」页里的完整说明块 */
  variant?: 'compact' | 'full';
}

export function PhoneAppDownload({ variant = 'compact' }: Props) {
  const { message } = AntApp.useApp();
  const isMobile = useIsMobile();
  const [showQr, setShowQr] = useState(false);
  const release = useQuery({
    queryKey: ['app-release'],
    queryFn: fetchAppRelease,
    // 每次进页面都重新问一次：构建脚本换了包，这里立刻是新数字
    staleTime: 0,
    refetchOnMount: 'always',
    retry: false,
  });

  const info = release.data;
  const download = appDownloadUrl(info ?? { download_path: '' });
  const infoText = info?.available
    ? describeAppRelease(info)
    : release.isLoading
      ? '正在读取安装包信息…'
      : '这台服务器上还没有准备好安装包';

  async function copyDownloadUrl(url: string) {
    try {
      await navigator.clipboard.writeText(url);
      message.success('已复制下载地址');
    } catch {
      message.warning('浏览器不允许自动复制，请手动选中复制');
    }
  }

  const qrSheet = (
    <ResponsiveSheet
      open={showQr}
      onClose={() => setShowQr(false)}
      title="用手机装这个应用"
      desktopWidth={520}
    >
      <Typography.Paragraph type="secondary">
        用手机相机或微信「扫一扫」下面这个二维码，手机会直接开始下载安装包；
        下完点开，允许「安装未知应用」就能装上。装好后在 App 里点「找电脑」即可连上。
      </Typography.Paragraph>
      <ShareLinkBox
        url={download.url}
        warning={download.warning}
        notice="这个地址永远指向服务器上最新的安装包（版本与构建号见上）。"
        onCopy={(url) => void copyDownloadUrl(url)}
      />
    </ResponsiveSheet>
  );

  if (variant === 'compact') {
    return (
      <div style={{ padding: '10px 16px' }} data-testid="phone-app-download-compact">
        <Space align="center" size={10} style={{ width: '100%' }} wrap>
          <span style={{ fontSize: 18, color: '#389e0d' }}>
            <AndroidOutlined />
          </span>
          <Typography.Text strong style={{ fontSize: 15 }}>
            配套手机应用
          </Typography.Text>
          {info?.available && (
            <>
              <Button
                type="primary"
                size="small"
                icon={<DownloadOutlined />}
                href={download.url}
              >
                下载
              </Button>
              <Button
                type="link"
                size="small"
                icon={<QrcodeOutlined />}
                onClick={() => setShowQr(true)}
              >
                二维码
              </Button>
            </>
          )}
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            {infoText}
          </Typography.Text>
        </Space>
        {qrSheet}
      </div>
    );
  }

  return (
    <div data-testid="phone-app-download-full">
      <Space direction="vertical" size={10} style={{ width: '100%' }}>
        <Space align="start" size={10}>
          <span style={{ fontSize: 20, color: '#389e0d', lineHeight: '26px' }}>
            <AndroidOutlined />
          </span>
          <div>
            <Typography.Text strong style={{ fontSize: 15 }}>
              局域网文件柜 · 安卓手机应用
            </Typography.Text>
            <div>
              <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                {infoText}
              </Typography.Text>
            </div>
          </div>
        </Space>

        {info?.available ? (
          <>
            <Space wrap>
              <Button type="primary" icon={<DownloadOutlined />} href={download.url}>
                {isMobile ? '下载安装' : '下载安装包'}
              </Button>
              {!isMobile && (
                <Button icon={<QrcodeOutlined />} onClick={() => setShowQr(true)}>
                  用手机扫码安装
                </Button>
              )}
            </Space>
            <Typography.Text type="secondary" style={{ fontSize: 12 }}>
              {isMobile
                ? '点「下载安装」就下到这台手机；安装时允许「安装未知应用」即可。'
                : '在电脑上点「下载安装包」会把 APK 下到电脑；要装到手机上，点「用手机扫码安装」扫一下最快。'}
            </Typography.Text>
          </>
        ) : (
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            安装包还没构建：在装有源码的电脑上跑一次
            <Typography.Text code>scripts\build-android.ps1</Typography.Text>
            ，刷新本页即可。
          </Typography.Text>
        )}
      </Space>
      {qrSheet}
    </div>
  );
}
