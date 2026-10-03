/**
 * 「广场」页（五阶段改造 ⑤，AGENTS.md §15）——用户 2026-09-21 明确要求：
 * "在侧边栏加一个广场按钮，要看到什么用户在线，用户可以在设置里隐藏自己，
 *   默认只要你在线了，就会显示你在线"；以及"同一个账号在手机端和电脑端同时登录，
 *   侧边栏就会有一个默认的互传文件按钮"。
 *
 * 这一页做三件事：
 *   1. **谁在线** —— 局域网/热点里同一套服务上的人，每张卡上直接「发文件给他」；
 *   2. **我的设备** —— 同一账号的其它设备；按 §14.2 明确写出"同一账号文件本来就是同一份，
 *      不需要传输"，并给一个二维码方便在另一台设备上打开；
 *   3. 空状态用大白话告诉用户"怎么才能让别人出现在这里"。
 *
 * 隐私：默认可见（用户要求），可在「设置」里隐藏；隐藏后别人看不到你，你自己仍能看到自己的设备。
 */
import { useState } from 'react';
import {
  Alert,
  App as AntApp,
  Button,
  Card,
  Empty,
  List,
  Space,
  Spin,
  Tag,
  Typography,
} from 'antd';
import {
  DesktopOutlined,
  MobileOutlined,
  ReloadOutlined,
  SendOutlined,
  UserOutlined,
} from '@ant-design/icons';


import { ResponsiveSheet } from '../components/ResponsiveSheet';
import { SendFilePicker } from '../components/SendFilePicker';
import { ShareLinkBox } from '../components/ShareLinkBox';
import { ensureDeviceId, usePresenceHeartbeat } from '../features/presence/usePresence';
import { buildSiteUrl } from '../services/publicUrl';
import type { OnlineUser } from '../services/presenceApi';

/** 平台图标：手机 / 电脑，一眼能看出对方用什么设备 */
function PlatformIcon({ platform }: { platform: string }) {
  return platform === 'android' || platform === 'ios' ? <MobileOutlined /> : <DesktopOutlined />;
}

function platformLabel(platform: string): string {
  if (platform === 'android') return '手机';
  if (platform === 'ios') return 'iPhone';
  if (platform === 'windows') return '电脑';
  return '浏览器';
}

export function PlazaPage() {
  const presence = usePresenceHeartbeat();
  const { message } = AntApp.useApp();
  const [target, setTarget] = useState<OnlineUser | null>(null);
  const [showQr, setShowQr] = useState(false);

  async function copySiteUrl(url: string) {
    try {
      await navigator.clipboard.writeText(url);
      message.success('已复制到剪贴板，发给对方即可');
    } catch {
      message.warning('浏览器不允许自动复制，请手动选中复制');
    }
  }

  const online = presence.data?.online ?? [];
  const myDeviceId = ensureDeviceId();
  const otherDevices = (presence.data?.my_devices ?? []).filter(
    (device) => device.device_id !== myDeviceId,
  );
  // ⛔ 不能用 window.location.origin：电脑上用 127.0.0.1 打开时，
  //    这个二维码在手机上是"打开手机自己"，必然打不开（与分享链接同一个坑，AGENTS §14.4）
  const { url: siteUrl, warning: siteWarning } = buildSiteUrl();

  return (
    <Space direction="vertical" size={12} style={{ width: '100%' }}>
      <Card
        title={<Space><UserOutlined /><span>谁在线</span></Space>}
        extra={
          <Button
            icon={<ReloadOutlined />}
            loading={presence.isFetching}
            onClick={() => void presence.refetch()}
          >
            刷新
          </Button>
        }
      >
        <Typography.Paragraph type="secondary" style={{ marginBottom: 12 }}>
          这里显示<Typography.Text strong>连着同一个网络</Typography.Text>
          （同一个 WiFi 或你的手机热点）并且在用这套服务的人。
        </Typography.Paragraph>

        {presence.isLoading && <Spin />}

        {!presence.isLoading && online.length === 0 && (
          <Empty
            image={Empty.PRESENTED_IMAGE_SIMPLE}
            description={
              <span>
                现在没有别人在线。
                <br />
                让同事连上同一个 WiFi（或你的手机热点），打开你给的这个地址，他就会出现在这里。
              </span>
            }
          />
        )}

        <List
          dataSource={online}
          renderItem={(person) => (
            <List.Item
              actions={[
                <Button
                  key="send"
                  type="primary"
                  size="large"
                  icon={<SendOutlined />}
                  onClick={() => setTarget(person)}
                >
                  发文件给他
                </Button>,
              ]}
            >
              <List.Item.Meta
                avatar={<PlatformIcon platform={person.platform} />}
                title={person.username}
                description={
                  <Space size={6} wrap>
                    <Tag>{platformLabel(person.platform)}</Tag>
                    <Typography.Text type="secondary">{person.device_name}</Typography.Text>
                  </Space>
                }
              />
            </List.Item>
          )}
        />
      </Card>

      <Card title="我的设备">
        <Alert
          type="info"
          showIcon
          style={{ marginBottom: 12 }}
          message="同一个账号的文件本来就是同一份，不需要互传"
          description="手机和电脑登录同一个账号，看到的就是同一批文件：在「我的文件」里直接看、直接下载即可。"
        />

        {otherDevices.length === 0 ? (
          <Typography.Text type="secondary">
            这个账号目前只有本机在线。用手机登录同一个账号（打开下面的地址），这里就会显示出来。
          </Typography.Text>
        ) : (
          <List
            dataSource={otherDevices}
            renderItem={(device) => (
              <List.Item>
                <List.Item.Meta
                  avatar={<PlatformIcon platform={device.platform} />}
                  title={`${device.device_name}（在线）`}
                  description={platformLabel(device.platform)}
                />
              </List.Item>
            )}
          />
        )}

        <Space wrap style={{ marginTop: 12 }}>
          <Button onClick={() => setShowQr(true)}>在另一台设备上打开（显示二维码）</Button>
        </Space>

        <ResponsiveSheet
          open={showQr}
          onClose={() => setShowQr(false)}
          title="在另一台设备上打开"
          desktopWidth={520}
        >
          <Typography.Paragraph type="secondary">
            用手机相机 / 微信「扫一扫」下面这个二维码，就能在手机上打开同一套服务；
            登录同一个账号即可看到同一批文件。
          </Typography.Paragraph>
          <ShareLinkBox url={siteUrl} warning={siteWarning} onCopy={(url) => void copySiteUrl(url)} />
        </ResponsiveSheet>
      </Card>

      <SendFilePicker target={target} onClose={() => setTarget(null)} />
    </Space>
  );
}
