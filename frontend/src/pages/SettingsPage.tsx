/**
 * 个人设置页（docs/frontend.md §2.2 左侧导航第 5 项）。
 *
 * 当前只做「只读的账号与存储信息」：用户名、邮箱、角色、状态、配额与已用空间。
 * 改密码、改邮箱等写操作需要后端提供对应接口，属于后续阶段，这里明确标注而不是
 * 放一个点了没反应的按钮。
 */
import { Card, Descriptions, Progress, Space, Tag, Typography } from 'antd';

import { formatBytes } from '../features/files/listView';
import { useAuth } from '../features/auth/AuthContext';

export function SettingsPage() {
  const { user } = useAuth();

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

      <Card title="安全">
        <Typography.Paragraph type="secondary" style={{ marginBottom: 0 }}>
          当前登录凭证保存在浏览器会话存储中（关闭标签页即失效），不使用 localStorage。
          修改密码、二次验证等设置项需要后端接口支持，将在后续阶段提供。
        </Typography.Paragraph>
      </Card>
    </Space>
  );
}
