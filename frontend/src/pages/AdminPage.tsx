/**
 * 管理员页面（阶段十三第 19 条）：用户管理 / 系统存储统计 / 审计日志。
 *
 * 红线（任务书 §13）：页面不展示密码、密码哈希、Token 或敏感配置。
 * 后端返回的用户对象本来就不含哈希，这里也不做任何"隐藏字段"的假设。
 *
 * 前端隐藏入口**不是**权限控制：所有请求都由后端按 ADMIN 角色判定，
 * 非管理员即使手动打开本页也只会看到 403 提示。
 */
import { useState } from 'react';
import {
  Alert,
  Button,
  Card,
  Descriptions,
  Form,
  Input,
  InputNumber,
  Modal,
  Popconfirm,
  Result,
  Space,
  Statistic,
  Table,
  Tabs,
  Tag,
  Typography,
  message,
} from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { useAuth } from '../features/auth/AuthContext';
import { InvitePanel } from '../components/InvitePanel';
import { formatBytes } from '../features/files/listView';
import { ApiError } from '../services/api';
import { adminApi } from '../services/adminApi';
import type { AdminUser, AuditLogEntry } from '../types/admin';

const { Title, Text } = Typography;

function describeError(error: unknown): string {
  if (error instanceof ApiError) {
    if (error.code === 'FORBIDDEN') {
      return '需要管理员权限';
    }
    return error.message;
  }
  return '操作失败，请稍后重试';
}

function formatDateTime(value: string): string {
  const parsed = new Date(value);
  return Number.isNaN(parsed.getTime()) ? value : parsed.toLocaleString();
}

function UsersTab() {
  const queryClient = useQueryClient();
  const [page, setPage] = useState(1);
  const [search, setSearch] = useState('');
  const [quotaTarget, setQuotaTarget] = useState<AdminUser | null>(null);
  const [quotaForm] = Form.useForm<{ storage_quota_bytes: number }>();

  const usersQuery = useQuery({
    queryKey: ['admin', 'users', page, search],
    queryFn: () => adminApi.listUsers({ page, page_size: 20, search, sort_by: 'username' }),
  });

  const statusMutation = useMutation({
    mutationFn: ({ id, status }: { id: string; status: 'ACTIVE' | 'DISABLED' }) =>
      adminApi.setUserStatus(id, status),
    onSuccess: () => {
      message.success('已更新用户状态');
      void queryClient.invalidateQueries({ queryKey: ['admin', 'users'] });
    },
    onError: (error) => message.error(describeError(error)),
  });

  const quotaMutation = useMutation({
    mutationFn: ({ id, quota }: { id: string; quota: number }) =>
      adminApi.setUserQuota(id, quota),
    onSuccess: () => {
      message.success('已更新配额');
      setQuotaTarget(null);
      void queryClient.invalidateQueries({ queryKey: ['admin', 'users'] });
    },
    onError: (error) => message.error(describeError(error)),
  });

  const columns: ColumnsType<AdminUser> = [
    { title: '用户名', dataIndex: 'username', key: 'username' },
    { title: '邮箱', dataIndex: 'email', key: 'email' },
    {
      title: '角色',
      dataIndex: 'role',
      key: 'role',
      render: (role: AdminUser['role']) => (
        <Tag color={role === 'ADMIN' ? 'gold' : 'blue'}>{role === 'ADMIN' ? '管理员' : '普通用户'}</Tag>
      ),
    },
    {
      title: '状态',
      dataIndex: 'status',
      key: 'status',
      render: (status: AdminUser['status']) => (
        <Tag color={status === 'ACTIVE' ? 'green' : 'red'}>
          {status === 'ACTIVE' ? '正常' : '已禁用'}
        </Tag>
      ),
    },
    {
      title: '已用 / 配额',
      key: 'storage',
      render: (_, user) => (
        <Text>
          {formatBytes(user.used_storage_bytes)} / {formatBytes(user.storage_quota_bytes)}
        </Text>
      ),
    },
    {
      title: '创建时间',
      dataIndex: 'created_at',
      key: 'created_at',
      render: (value: string) => formatDateTime(value),
    },
    {
      title: '操作',
      key: 'actions',
      render: (_, user) => (
        <Space>
          {user.status === 'ACTIVE' ? (
            <Popconfirm
              title="禁用该用户？"
              description="禁用后该用户无法登录，已签发的 Token 也会立即失效。"
              okText="禁用"
              cancelText="取消"
              onConfirm={() => statusMutation.mutate({ id: user.id, status: 'DISABLED' })}
            >
              <Button size="small" danger>
                禁用
              </Button>
            </Popconfirm>
          ) : (
            <Button
              size="small"
              onClick={() => statusMutation.mutate({ id: user.id, status: 'ACTIVE' })}
            >
              启用
            </Button>
          )}
          <Button
            size="small"
            onClick={() => {
              setQuotaTarget(user);
              quotaForm.setFieldsValue({ storage_quota_bytes: user.storage_quota_bytes });
            }}
          >
            改配额
          </Button>
        </Space>
      ),
    },
  ];

  if (usersQuery.isError) {
    return <Alert type="error" showIcon message={describeError(usersQuery.error)} />;
  }

  return (
    <Space direction="vertical" style={{ width: '100%' }} size="middle">
      <Space>
        <Input.Search
          allowClear
          placeholder="按用户名或邮箱搜索"
          style={{ width: 280 }}
          onSearch={(value) => {
            setSearch(value);
            setPage(1);
          }}
        />
        <Button onClick={() => void usersQuery.refetch()}>刷新</Button>
      </Space>

      <Table<AdminUser>
        rowKey="id"
        loading={usersQuery.isLoading}
        columns={columns}
        dataSource={usersQuery.data?.items ?? []}
        // 手机上也让表格**自己内部**横滑，不要把整页撑宽（否则底部导航会被推出屏幕）
        scroll={{ x: 720 }}
        pagination={{
          current: page,
          pageSize: 20,
          total: usersQuery.data?.total ?? 0,
          onChange: setPage,
          showSizeChanger: false,
        }}
      />

      <Modal
        open={quotaTarget !== null}
        title={`修改配额：${quotaTarget?.username ?? ''}`}
        okText="保存"
        cancelText="取消"
        confirmLoading={quotaMutation.isPending}
        onCancel={() => setQuotaTarget(null)}
        onOk={() => {
          void quotaForm.validateFields().then((values) => {
            if (quotaTarget) {
              quotaMutation.mutate({ id: quotaTarget.id, quota: values.storage_quota_bytes });
            }
          });
        }}
      >
        <Form form={quotaForm} layout="vertical">
          <Form.Item
            name="storage_quota_bytes"
            label="存储配额（字节）"
            rules={[{ required: true, message: '请填写配额' }]}
          >
            <InputNumber min={1} style={{ width: '100%' }} />
          </Form.Item>
          <Text type="secondary">
            不能设为 0；调小到低于已用量后，该用户将无法继续上传，直到清理出空间。
          </Text>
        </Form>
      </Modal>
    </Space>
  );
}

function StorageTab() {
  const statsQuery = useQuery({
    queryKey: ['admin', 'storage'],
    queryFn: () => adminApi.storageStats(),
  });

  if (statsQuery.isError) {
    return <Alert type="error" showIcon message={describeError(statsQuery.error)} />;
  }
  const stats = statsQuery.data;

  return (
    <Space direction="vertical" style={{ width: '100%' }} size="middle">
      <Space size="large" wrap>
        <Card>
          <Statistic title="用户数" value={stats?.user_count ?? 0} />
        </Card>
        <Card>
          <Statistic title="文件数" value={stats?.file_count ?? 0} />
        </Card>
        <Card>
          <Statistic title="版本数" value={stats?.version_count ?? 0} />
        </Card>
        <Card>
          <Statistic title="已完成上传占用" value={formatBytes(stats?.used_bytes ?? 0)} />
        </Card>
        <Card>
          <Statistic title="磁盘可用" value={formatBytes(stats?.free_bytes ?? 0)} />
        </Card>
        <Card>
          <Statistic title="进行中的上传会话" value={stats?.active_uploads ?? 0} />
        </Card>
      </Space>
      <Descriptions column={1} size="small">
        <Descriptions.Item label="说明">
          统计口径：占用按文件版本累计（历史版本也占空间）；磁盘可用来自对象存储所在卷。
          这里的数字与 <Text code>/metrics</Text> 暴露的指标同源。
        </Descriptions.Item>
      </Descriptions>
    </Space>
  );
}

function AuditTab() {
  const [page, setPage] = useState(1);
  const [action, setAction] = useState('');

  const logsQuery = useQuery({
    queryKey: ['admin', 'audit', page, action],
    queryFn: () =>
      adminApi.listAuditLogs({ page, page_size: 20, ...(action ? { action } : {}) }),
  });

  const columns: ColumnsType<AuditLogEntry> = [
    { title: '时间', dataIndex: 'created_at', key: 'created_at', render: formatDateTime },
    { title: '操作', dataIndex: 'action', key: 'action' },
    { title: '资源类型', dataIndex: 'resource_type', key: 'resource_type' },
    { title: '资源 ID', dataIndex: 'resource_id', key: 'resource_id' },
    { title: '操作者', dataIndex: 'actor_id', key: 'actor_id' },
    {
      title: '详情',
      dataIndex: 'detail',
      key: 'detail',
      render: (detail: Record<string, unknown>) => (
        <Text style={{ fontSize: 12 }}>{JSON.stringify(detail)}</Text>
      ),
    },
  ];

  if (logsQuery.isError) {
    return <Alert type="error" showIcon message={describeError(logsQuery.error)} />;
  }

  return (
    <Space direction="vertical" style={{ width: '100%' }} size="middle">
      <Space>
        <Input.Search
          allowClear
          placeholder="按操作名筛选，例如 admin_disable_user"
          style={{ width: 320 }}
          onSearch={(value) => {
            setAction(value);
            setPage(1);
          }}
        />
        <Button onClick={() => void logsQuery.refetch()}>刷新</Button>
      </Space>
      <Alert
        type="info"
        showIcon
        message="查看审计日志本身也会被记录（admin_query_audit_logs）"
        description="审计写入失败时业务操作不会被回滚，但会在服务端日志里留下 error 记录。"
      />
      <Table<AuditLogEntry>
        rowKey="id"
        loading={logsQuery.isLoading}
        columns={columns}
        dataSource={logsQuery.data?.items ?? []}
        // 同上：表格内部横滑，别把整页撑宽
        scroll={{ x: 720 }}
        pagination={{
          current: page,
          pageSize: 20,
          total: logsQuery.data?.total ?? 0,
          onChange: setPage,
          showSizeChanger: false,
        }}
      />
    </Space>
  );
}

export function AdminPage() {
  const { user } = useAuth();

  // 前端只决定"要不要显示"，真正的权限在后端；非管理员打开也只会拿到 403
  if (user?.role !== 'ADMIN') {
    return (
      <Result
        status="403"
        title="403"
        subTitle="需要管理员权限才能访问本页。"
      />
    );
  }

  return (
    <Space direction="vertical" style={{ width: '100%' }} size="large">
      <Title level={3} style={{ margin: 0 }}>
        系统管理
      </Title>
      <Tabs
        items={[
          { key: 'users', label: '用户管理', children: <UsersTab /> },
          // 邀请制注册（用户 2026-10-02）：管理员发链接，别人凭链接才能注册
          { key: 'invites', label: '邀请注册', children: <InvitePanel /> },
          { key: 'storage', label: '存储统计', children: <StorageTab /> },
          { key: 'audit', label: '审计日志', children: <AuditTab /> },
        ]}
      />
    </Space>
  );
}
