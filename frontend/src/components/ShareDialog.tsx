/**
 * 分享弹窗（docs/permissions.md §7）。
 *
 * 支持：创建链接（只读/可写、有效期、下载次数上限）、复制链接、撤销、查看访问次数。
 * 明文 Token **只在创建成功后展示一次**：关闭弹窗就再也拿不到，需要新 Token 只能重新创建。
 */
import { useState } from 'react';
import {
  Alert,
  App as AntApp,
  Button,
  Card,
  Empty,
  Form,
  InputNumber,
  Popconfirm,
  Select,
  Space,
  Spin,
  Tag,
  Typography,
} from 'antd';
import { LinkOutlined } from '@ant-design/icons';

import {
  SHARE_PERMISSION_LABELS,
  sharingErrorMessage,
  useCreateShare,
  useRevokeShare,
  useShares,
} from '../features/sharing/useSharing';
import { buildShareUrl } from '../services/publicUrl';
import { refreshServerLanAddress } from '../services/networkApi';
import { ResponsiveSheet } from './ResponsiveSheet';
import { ShareLinkBox } from './ShareLinkBox';
import type { FileMeta } from '../types/file';
import type { Folder } from '../types/folder';
import type { ShareLink, SharePermission } from '../types/share';

interface Props {
  file?: FileMeta | null;
  folder?: Folder | null;
  open: boolean;
  onClose: () => void;
}

const EXPIRY_OPTIONS = [
  { value: 0, label: '不过期' },
  { value: 3600, label: '1 小时' },
  { value: 86400, label: '1 天' },
  { value: 7 * 86400, label: '7 天' },
  { value: 30 * 86400, label: '30 天' },
];

function shareStatus(link: ShareLink): { text: string; color: string } {
  if (link.revoked) return { text: '已撤销', color: 'default' };
  if (link.expires_at && new Date(link.expires_at).getTime() < Date.now()) {
    return { text: '已过期', color: 'warning' };
  }
  if (link.max_downloads !== null && link.download_count >= link.max_downloads) {
    return { text: '次数已用完', color: 'warning' };
  }
  return { text: '有效', color: 'green' };
}

export function ShareDialog({ file, folder, open, onClose }: Props) {
  const { message } = AntApp.useApp();
  const resourceId = file?.id ?? folder?.id;
  const resourceName = file?.name ?? folder?.name ?? '';
  const isFolder = Boolean(folder);

  const shares = useShares(resourceId, open);
  const createShare = useCreateShare(resourceId);
  const revokeShare = useRevokeShare(resourceId);
  const [form] = Form.useForm<{ permission: SharePermission; expires: number; maxDownloads?: number }>();
  const [createdToken, setCreatedToken] = useState<{ token: string; url: string; warning?: string } | null>(
    null,
  );

  async function handleCreate() {
    const values = await form.validateFields();
    try {
      const created = await createShare.mutateAsync({
        permission: values.permission,
        expiresInSeconds: values.expires ? Number(values.expires) : undefined,
        maxDownloads: values.maxDownloads ? Number(values.maxDownloads) : undefined,
      });
      // 先问一次服务端"当前的局域网地址是多少"：用户可能刚换过网络（WiFi ↔ 热点），
      // 用打开页面时的旧地址拼出来的链接，对方手机打不开
      await refreshServerLanAddress();
      const built = buildShareUrl(created.token ?? '');
      setCreatedToken({ token: created.token ?? '', url: built.url, warning: built.warning });
      message.success('分享链接已创建，请立刻复制（只显示这一次）');
    } catch (error) {
      message.error(sharingErrorMessage(error));
    }
  }

  async function handleCopy(text: string) {
    try {
      await navigator.clipboard.writeText(text);
      message.success('已复制到剪贴板');
    } catch {
      // 剪贴板不可用时退回手动选择
      message.warning('浏览器不允许自动复制，请手动选中复制');
    }
  }

  async function handleRevoke(link: ShareLink) {
    try {
      await revokeShare.mutateAsync(link.id);
      message.success('已撤销该分享链接');
      if (createdToken) setCreatedToken(null);
    } catch (error) {
      message.error(sharingErrorMessage(error));
    }
  }

  const items = shares.data?.items ?? [];

  return (
    <ResponsiveSheet
      open={open}
      desktopWidth={720}
      title={
        <Space>
          <LinkOutlined />
          <span>分享{isFolder ? '文件夹' : '文件'}：{resourceName}</span>
        </Space>
      }
      onClose={() => {
        // 关闭后明文 Token 不再展示（服务端也没有保存它）
        setCreatedToken(null);
        onClose();
      }}
    >
      <Form
        form={form}
        layout="inline"
        initialValues={{ permission: 'READ', expires: 7 * 86400 }}
        style={{ marginBottom: 16 }}
      >
        <Form.Item label="权限" name="permission">
          <Select
            style={{ width: 120 }}
            options={[
              { value: 'READ', label: SHARE_PERMISSION_LABELS.READ },
              { value: 'WRITE', label: SHARE_PERMISSION_LABELS.WRITE },
            ]}
          />
        </Form.Item>
        <Form.Item label="有效期" name="expires">
          <Select style={{ width: 130 }} options={EXPIRY_OPTIONS} />
        </Form.Item>
        <Form.Item label="下载上限" name="maxDownloads">
          <InputNumber min={1} max={100000} placeholder="不限" style={{ width: 110 }} />
        </Form.Item>
        <Form.Item>
          <Button type="primary" loading={createShare.isPending} onClick={() => void handleCreate()}>
            创建链接
          </Button>
        </Form.Item>
      </Form>

      {isFolder && (
        <Alert
          type="info"
          showIcon
          style={{ marginBottom: 12 }}
          message="文件夹分享支持浏览，暂不支持打包下载"
        />
      )}

      {createdToken?.warning && (
        <Alert type="warning" showIcon style={{ marginBottom: 12 }} message={createdToken.warning} />
      )}

      {createdToken && (
        <Alert
          type="success"
          showIcon
          style={{ marginBottom: 12 }}
          message="链接已创建，明文 Token 只显示这一次"
          description={
            <ShareLinkBox
              url={createdToken.url}
              notice="关闭弹窗后无法再次查看；需要新链接请重新创建（旧链接可以继续用，也可以在这里撤销）。"
              onCopy={(text) => void handleCopy(text)}
            />
          }
        />
      )}

      {shares.isLoading && <Spin />}
      {!shares.isLoading && items.length === 0 && (
        <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="还没有分享链接" />
      )}

      {items.map((link) => {
        const status = shareStatus(link);
        return (
          <Card key={link.id} size="small" style={{ marginBottom: 8 }}>
            <Space style={{ width: '100%', justifyContent: 'space-between' }} wrap>
              <Space size={6} wrap>
                <Tag color={status.color}>{status.text}</Tag>
                <Tag>{SHARE_PERMISSION_LABELS[link.permission]}</Tag>
                <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                  访问 {link.download_count}
                  {link.max_downloads !== null ? ` / ${link.max_downloads}` : ''} 次
                </Typography.Text>
                <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                  {link.expires_at
                    ? `到期 ${new Date(link.expires_at).toLocaleString()}`
                    : '不过期'}
                </Typography.Text>
              </Space>
              {!link.revoked && (
                <Popconfirm
                  title="撤销这个分享链接？"
                  description="撤销后链接立即失效，已下载的内容不受影响。"
                  okText="撤销"
                  okButtonProps={{ danger: true }}
                  cancelText="取消"
                  onConfirm={() => void handleRevoke(link)}
                >
                  <Button type="link" size="small" danger>
                    撤销
                  </Button>
                </Popconfirm>
              )}
            </Space>
          </Card>
        );
      })}
    </ResponsiveSheet>
  );
}
