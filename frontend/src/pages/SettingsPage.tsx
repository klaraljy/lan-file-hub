/**
 * 个人设置页（docs/frontend.md §2.2 左侧导航第 5 项）。
 *
 * 当前做两件事：
 *   1. 只读的账号与存储信息（用户名、邮箱、角色、状态、配额与已用空间）；
 *   2. **在线可见性开关**（阶段 ⑤ 广场）：默认可见，关掉后别人在广场看不到我。
 * 改密码、改邮箱等写操作需要后端提供对应接口，属于后续阶段，这里明确标注而不是
 * 放一个点了没反应的按钮。
 */
import { App as AntApp, Card, Descriptions, Progress, Space, Switch, Tag, Typography } from 'antd';
import { useMutation } from '@tanstack/react-query';

import { formatBytes } from '../features/files/listView';
import { useAuth } from '../features/auth/AuthContext';
import { usePresenceHeartbeat } from '../features/presence/usePresence';
import { presenceApi } from '../services/presenceApi';

export function SettingsPage() {
  const { user } = useAuth();
  const { message } = AntApp.useApp();
  const presence = usePresenceHeartbeat();

  // 默认可见（用户要求"只要在线就默认显示"）；服务端返回的就是当前值
  const visible = presence.data?.visible ?? true;

  const toggle = useMutation({
    mutationFn: (next: boolean) => presenceApi.setVisible(next),
    onSuccess: async (next) => {
      message.success(next ? '别人现在能在广场看到你在线' : '已隐藏：别人在广场看不到你在线');
      await presence.refetch();
    },
    onError: () => message.error('设置失败，请稍后重试'),
  });

  if (!user) {
    // 外壳已经保证进入这里是登录态，这里只是防御性兜底
    return <Card title="设置">无法读取当前账号信息，请重新登录。</Card>;
  }

  const quota = user.storage_quota_bytes;
  const used = user.used_storage_bytes;
  const percent = quota > 0 ? Math.min(100, Math.round((used / quota) * 100)) : 0;

  return (
    <Space direction="vertical" size="middle" style={{ width: '100%' }}>
      <Card title="账号信息">
        <Descriptions column={1} size="small">
          <Descriptions.Item label="用户名">{user.username}</Descriptions.Item>
          <Descriptions.Item label="邮箱">{user.email}</Descriptions.Item>
          <Descriptions.Item label="角色">
            <Tag color={user.role === 'ADMIN' ? 'gold' : 'blue'}>
              {user.role === 'ADMIN' ? '管理员' : '普通用户'}
            </Tag>
          </Descriptions.Item>
          <Descriptions.Item label="状态">
            <Tag color={user.status === 'ACTIVE' ? 'green' : 'red'}>
              {user.status === 'ACTIVE' ? '正常' : '已禁用'}
            </Tag>
          </Descriptions.Item>
        </Descriptions>
      </Card>

      <Card title="存储用量">
        <Space direction="vertical" style={{ width: '100%' }}>
          <Typography.Text>
            已用 {formatBytes(used)} / 配额 {formatBytes(quota)}
          </Typography.Text>
          <Progress
            percent={percent}
            status={percent >= 90 ? 'exception' : 'normal'}
            style={{ maxWidth: 480 }}
          />
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            回收站中的文件仍占用空间；历史版本也计入用量（永久删除或超过保留期后才会释放）。
          </Typography.Text>
        </Space>
      </Card>

      <Card title="广场与隐私">
        <Space direction="vertical" size={8} style={{ width: '100%' }}>
          <Space size={12} wrap>
            <Switch
              checked={visible}
              loading={toggle.isPending}
              onChange={(next) => toggle.mutate(next)}
            />
            <Typography.Text>
              {visible ? '让别人看到我在线' : '隐藏我的在线状态'}
            </Typography.Text>
          </Space>
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            开着的效果：同一网络里的同事打开「广场」就能看到你在线，并可以直接把文件发给你。
            关掉后别人看不到你，但你自己仍然能看到自己的设备；你随时可以再打开。
          </Typography.Text>
        </Space>
      </Card>

      <Card title="安全">
        <Typography.Paragraph type="secondary" style={{ marginBottom: 0 }}>
          当前登录凭证保存在浏览器会话存储中（关闭标签页即失效），不使用 localStorage。
          修改密码、二次验证等设置项需要后端接口支持，将在后续阶段提供。
        </Typography.Paragraph>
      </Card>
    </Space>
  );
}
