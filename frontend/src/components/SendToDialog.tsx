/**
 * 「发送给…」弹窗（AGENTS.md §14.3 / §14.4，docs/frontend.md §2.5）。
 *
 * 解决的问题：用户不懂"权限""用户 ID"这些词，他只想"把这个文件发给某个人"。
 * 所以这里就是三步：**选人 → 选对方能做什么 → 发送**；
 * 如果对方还没有账号，同一个弹窗里直接生成一条链接（链接按 publicUrl.ts 的规则，
 * 保证对方手机上点得开）。
 */
import { useState } from 'react';
import {
  Alert,
  App as AntApp,
  Button,
  Divider,
  Select,
  Space,
  Spin,
  Typography,
} from 'antd';
import { LinkOutlined, SendOutlined, UserOutlined } from '@ant-design/icons';

import {
  PERMISSION_LABELS,
  sharingErrorMessage,
  useCreateShare,
  useGrantPermission,
} from '../features/sharing/useSharing';
import { useUserLookup } from '../features/sharing/useUserLookup';
import { buildShareUrl } from '../services/publicUrl';
import { refreshServerLanAddress } from '../services/networkApi';
import { ResponsiveSheet } from './ResponsiveSheet';
import { ShareLinkBox } from './ShareLinkBox';
import type { UserLookupItem } from '../services/usersApi';
import type { PermissionValue } from '../types/share';

interface Props {
  open: boolean;
  onClose: () => void;
  resourceId: string;
  resourceName: string;
  isFolder: boolean;
}

/** 链接默认 7 天有效（与「分享」弹窗的默认值保持一致） */
const LINK_EXPIRES_SECONDS = 7 * 86400;

export function SendToDialog({ open, onClose, resourceId, resourceName, isFolder }: Props) {
  const { message } = AntApp.useApp();
  const [keyword, setKeyword] = useState('');
  const [selected, setSelected] = useState<UserLookupItem | null>(null);
  const [permission, setPermission] = useState<PermissionValue>('READ');
  const [link, setLink] = useState<{ url: string; warning?: string } | null>(null);

  const lookup = useUserLookup(open ? keyword : '');
  const grant = useGrantPermission(resourceId);
  const createShare = useCreateShare(resourceId);

  const found = lookup.data ?? [];
  // 已经选过的人要留在下拉框里：否则改一下搜索词，界面上选中的人就"消失"了
  const options =
    selected && !found.some((item) => item.id === selected.id) ? [selected, ...found] : found;

  function reset() {
    setKeyword('');
    setSelected(null);
    setPermission('READ');
    setLink(null);
  }

  async function handleSend() {
    if (!selected) {
      message.warning('请先选择要发送给谁');
      return;
    }
    try {
      await grant.mutateAsync({ userId: selected.id, permission });
      message.success(
        `已发送给 ${selected.username}：他/她登录后在左侧「分享给我的」里就能看到并下载`,
      );
      reset();
      onClose();
    } catch (error) {
      message.error(sharingErrorMessage(error));
    }
  }

  async function handleCreateLink() {
    try {
      const created = await createShare.mutateAsync({
        permission: 'READ',
        expiresInSeconds: LINK_EXPIRES_SECONDS,
      });
      // 先刷新一次服务端上报的局域网地址（用户可能刚换过网络，旧地址对方打不开）
      await refreshServerLanAddress();
      setLink(buildShareUrl(created.token ?? ''));
      message.success('链接已生成，请立刻复制发给对方（只显示这一次）');
    } catch (error) {
      message.error(sharingErrorMessage(error));
    }
  }

  async function handleCopy(text: string) {
    try {
      await navigator.clipboard.writeText(text);
      message.success('已复制到剪贴板');
    } catch {
      // 剪贴板不可用（http 下的少数浏览器）时退回手动选择
      message.warning('浏览器不允许自动复制，请手动选中复制');
    }
  }

  function notFoundContent() {
    if (!lookup.ready) {
      return <Typography.Text type="secondary">输入至少 2 个字开始搜索</Typography.Text>;
    }
    if (lookup.isFetching) {
      return <Spin size="small" />;
    }
    return (
      <Typography.Text type="secondary">
        没找到这个人？他可能还没有账号 —— 用下面的「生成分享链接」发给他
      </Typography.Text>
    );
  }

  return (
    <ResponsiveSheet
      open={open}
      desktopWidth={640}
      title={
        <Space>
          <SendOutlined />
          <span>发送给…</span>
        </Space>
      }
      onClose={() => {
        reset();
        onClose();
      }}
    >
      <Alert
        type="info"
        showIcon
        style={{ marginBottom: 12 }}
        message={`把「${resourceName}」发给别人`}
        description="对方登录后，在左侧「分享给我的」里就能看到并下载。"
      />

      {isFolder && (
        <Alert
          type="warning"
          showIcon
          style={{ marginBottom: 12 }}
          message="这是文件夹：对方可以进去浏览并下载里面的文件"
        />
      )}

      <Space direction="vertical" size={10} style={{ width: '100%' }}>
        <div>
          <Typography.Text strong>1. 发给谁</Typography.Text>
          <Select<string>
            showSearch
            style={{ width: '100%', marginTop: 4 }}
            placeholder="输入对方的用户名或邮箱（至少 2 个字）"
            value={selected?.id}
            onChange={(value) => setSelected(options.find((item) => item.id === value) ?? null)}
            onSearch={setKeyword}
            filterOption={false}
            notFoundContent={notFoundContent()}
            loading={lookup.isFetching}
            suffixIcon={<UserOutlined />}
            options={options.map((item) => ({ value: item.id, label: item.username }))}
          />
        </div>

        <div>
          <Typography.Text strong>2. 对方能做什么</Typography.Text>
          <Select<PermissionValue>
            style={{ width: '100%', marginTop: 4 }}
            value={permission}
            onChange={setPermission}
            options={[
              { value: 'READ', label: PERMISSION_LABELS.READ },
              { value: 'WRITE', label: PERMISSION_LABELS.WRITE },
            ]}
          />
        </div>

        <Button
          type="primary"
          size="large"
          block
          icon={<SendOutlined />}
          loading={grant.isPending}
          onClick={() => void handleSend()}
        >
          发送
        </Button>
      </Space>

      <Divider plain>对方还没有账号？</Divider>

      <Space direction="vertical" size={8} style={{ width: '100%' }}>
        <Typography.Text type="secondary">
          生成一条链接直接发给他（微信、QQ 都行）：对方手机点开就能下载，
          <Typography.Text strong>不需要注册，也不需要登录</Typography.Text>。
        </Typography.Text>

        {!link && (
          <Button
            icon={<LinkOutlined />}
            loading={createShare.isPending}
            onClick={() => void handleCreateLink()}
          >
            生成分享链接
          </Button>
        )}

        {link && (
          <ShareLinkBox
            url={link.url}
            warning={link.warning}
            notice="链接只在这里显示这一次；对方打开就能下载。"
            onCopy={handleCopy}
          />
        )}
      </Space>
    </ResponsiveSheet>
  );
}
