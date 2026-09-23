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
import { useMemo, useState } from 'react';
import {
  Alert,
  App as AntApp,
  Button,
  Card,
  Empty,
  Input,
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
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { ResponsiveSheet } from '../components/ResponsiveSheet';
import { ShareLinkBox } from '../components/ShareLinkBox';
import { sharingErrorMessage, sharingKeys } from '../features/sharing/useSharing';
import { ensureDeviceId, usePresenceHeartbeat } from '../features/presence/usePresence';
import { filesApi } from '../services/filesApi';
import { foldersApi } from '../services/foldersApi';
import { sharingApi } from '../services/sharingApi';
import type { OnlineUser } from '../services/presenceApi';
import type { FileMeta } from '../types/file';

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

/** 选一个文件发给对方：列出「我的文件」，选中即授权（只读） */
function SendFileSheet({
  target,
  onClose,
}: {
  target: OnlineUser | null;
  onClose: () => void;
}) {
  const { message } = AntApp.useApp();
  const [keyword, setKeyword] = useState('');
  const [sendingId, setSendingId] = useState<string | null>(null);

  const root = useQuery({ queryKey: ['folders', 'root'], queryFn: () => foldersApi.root(), enabled: Boolean(target) });
  const files = useQuery({
    queryKey: ['files', 'root', 'plaza-picker'],
    queryFn: () => filesApi.list(root.data!.id, { page_size: 100, sort_by: 'name', sort_order: 'asc' }),
    enabled: Boolean(target) && Boolean(root.data?.id),
  });
  // 授权走既有的权限接口（与「发送给…」同一条链路）：文件所有者给对方 READ
  const queryClient = useQueryClient();
  const grant = useMutation({
    mutationFn: ({ fileId, userId }: { fileId: string; userId: string }) =>
      sharingApi.grant(fileId, userId, 'READ'),
    onSuccess: async (_data, variables) => {
      await queryClient.invalidateQueries({ queryKey: sharingKeys.permissions(variables.fileId) });
    },
  });

  const items = useMemo(() => {
    const list = files.data?.items ?? [];
    const trimmed = keyword.trim().toLowerCase();
    if (!trimmed) return list;
    return list.filter((file) => file.name.toLowerCase().includes(trimmed));
  }, [files.data, keyword]);

  async function send(file: FileMeta) {
    if (!target) return;
    setSendingId(file.id);
    try {
      await grant.mutateAsync({ fileId: file.id, userId: target.user_id });
      message.success(
        `已把「${file.name}」发给 ${target.username}：他登录后在左侧「分享给我的」里就能看到并下载`,
      );
      setKeyword('');
      onClose();
    } catch (error) {
      message.error(sharingErrorMessage(error));
    } finally {
      setSendingId(null);
    }
  }

  return (
    <ResponsiveSheet
      open={Boolean(target)}
      onClose={onClose}
      desktopWidth={640}
      title={<Space><SendOutlined /><span>发文件给 {target?.username ?? ''}</span></Space>}
    >
      <Alert
        type="info"
        showIcon
        style={{ marginBottom: 12 }}
        message="选一个文件发给他"
        description="对方登录后，在左侧「分享给我的」里就能看到并下载。"
      />

      <Input
        allowClear
        placeholder="按文件名过滤"
        value={keyword}
        onChange={(event) => setKeyword(event.target.value)}
        style={{ marginBottom: 8 }}
      />

      {files.isLoading && <Spin />}
      {!files.isLoading && items.length === 0 && (
        <Empty
          image={Empty.PRESENTED_IMAGE_SIMPLE}
          description={keyword ? '没有匹配的文件' : '你还没有上传过文件'}
        />
      )}

      <List
        dataSource={items}
        renderItem={(file) => (
          <List.Item
            actions={[
              <Button
                key="send"
                type="primary"
                size="large"
                loading={sendingId === file.id}
                onClick={() => void send(file)}
              >
                发给他
              </Button>,
            ]}
          >
            <List.Item.Meta
              title={file.name}
              description={`${(file.size_bytes / 1024 / 1024).toFixed(1)} MB`}
            />
          </List.Item>
        )}
      />
    </ResponsiveSheet>
  );
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
  const siteUrl = window.location.origin;

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
          <ShareLinkBox url={siteUrl} onCopy={(url) => void copySiteUrl(url)} />
        </ResponsiveSheet>
      </Card>

      <SendFileSheet target={target} onClose={() => setTarget(null)} />
    </Space>
  );
}
