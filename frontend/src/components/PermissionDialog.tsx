/**
 * 权限管理弹窗（阶段九）。
 *
 * 只有资源所有者（或管理员）能打开并修改；前端用有效权限决定是否展示入口，
 * **真正的判定在后端**（docs/permissions.md §2）。
 */
import { App as AntApp, Alert, Button, Form, Input, Popconfirm, Select, Space, Spin, Table, Tag, Typography } from 'antd';
import type { ColumnsType } from 'antd/es/table';

import {
  PERMISSION_LABELS,
  sharingErrorMessage,
  useGrantPermission,
  usePermissions,
  useRevokePermission,
} from '../features/sharing/useSharing';
import { ResponsiveSheet } from './ResponsiveSheet';
import type { FileMeta } from '../types/file';
import type { Folder } from '../types/folder';
import type { PermissionGrant, PermissionValue } from '../types/share';

interface Props {
  file?: FileMeta | null;
  folder?: Folder | null;
  open: boolean;
  onClose: () => void;
}

const LEVEL_COLORS: Record<PermissionValue, string> = {
  READ: 'blue',
  WRITE: 'orange',
  OWNER: 'red',
};

export function PermissionDialog({ file, folder, open, onClose }: Props) {
  const { message } = AntApp.useApp();
  const resourceId = file?.id ?? folder?.id;
  const resourceName = file?.name ?? folder?.name ?? '';

  const grants = usePermissions(resourceId, open);
  const grantMutation = useGrantPermission(resourceId);
  const revokeMutation = useRevokePermission(resourceId);
  const [form] = Form.useForm<{ userId: string; permission: PermissionValue }>();

  async function handleGrant() {
    const values = await form.validateFields();
    const userId = values.userId.trim();
    if (!userId) {
      message.error('请填写用户 ID');
      return;
    }
    try {
      const created = await grantMutation.mutateAsync({
        userId,
        permission: values.permission,
      });
      message.success(`已授予「${created.subject_name || userId}」${created.permission} 权限`);
      form.setFieldsValue({ userId: '' });
    } catch (error) {
      message.error(sharingErrorMessage(error));
    }
  }

  async function handleRevoke(item: PermissionGrant) {
    try {
      await revokeMutation.mutateAsync(item.subject_id);
      message.success(`已撤销「${item.subject_name || item.subject_id}」的权限`);
    } catch (error) {
      message.error(sharingErrorMessage(error));
    }
  }

  const columns: ColumnsType<PermissionGrant> = [
    {
      title: '用户',
      key: 'subject',
      render: (_value, item) => (
        <Space direction="vertical" size={0}>
          <span>{item.subject_name || '（用户已删除）'}</span>
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            {item.subject_id}
          </Typography.Text>
        </Space>
      ),
    },
    {
      title: '权限',
      key: 'permission',
      width: 220,
      render: (_value, item) => (
        <Space direction="vertical" size={0}>
          <Tag color={LEVEL_COLORS[item.permission]}>{item.permission}</Tag>
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            {PERMISSION_LABELS[item.permission]}
          </Typography.Text>
        </Space>
      ),
    },
    {
      title: '授予时间',
      key: 'created_at',
      width: 180,
      render: (_value, item) => new Date(item.created_at).toLocaleString(),
    },
    {
      title: '操作',
      key: 'actions',
      width: 100,
      render: (_value, item) => (
        <Popconfirm
          title={`撤销「${item.subject_name || item.subject_id}」的权限？`}
          okText="撤销"
          okButtonProps={{ danger: true }}
          cancelText="取消"
          onConfirm={() => void handleRevoke(item)}
        >
          <Button type="link" size="small" danger>
            撤销
          </Button>
        </Popconfirm>
      ),
    },
  ];

  return (
    <ResponsiveSheet
      open={open}
      desktopWidth={720}
      title={`权限管理：${resourceName}`}
      onClose={onClose}
    >
      <Alert
        type="info"
        showIcon
        style={{ marginBottom: 12 }}
        message="权限规则"
        description="READ 可查看与下载；WRITE 额外可上传、修改、删除；OWNER 还能管理权限与分享。子目录与文件会继承上级文件夹的权限，取路径上最高的一级。"
      />

      <Form
        form={form}
        layout="inline"
        initialValues={{ permission: 'READ' }}
        style={{ marginBottom: 16 }}
      >
        <Form.Item
          label="用户 ID"
          name="userId"
          rules={[{ required: true, message: '请填写用户 ID' }]}
        >
          <Input placeholder="用户 UUID" style={{ width: 280 }} />
        </Form.Item>
        <Form.Item label="权限" name="permission">
          <Select
            style={{ width: 110 }}
            options={[
              { value: 'READ', label: 'READ' },
              { value: 'WRITE', label: 'WRITE' },
              { value: 'OWNER', label: 'OWNER' },
            ]}
          />
        </Form.Item>
        <Form.Item>
          <Button type="primary" loading={grantMutation.isPending} onClick={() => void handleGrant()}>
            授权
          </Button>
        </Form.Item>
      </Form>

      {grants.isLoading && <Spin />}
      {!grants.isLoading && (
        <Table<PermissionGrant>
          rowKey="id"
          size="small"
          columns={columns}
          dataSource={grants.data ?? []}
          pagination={false}
          locale={{ emptyText: '还没有对其他用户授权（所有者自带全部权限）' }}
        />
      )}
    </ResponsiveSheet>
  );
}
