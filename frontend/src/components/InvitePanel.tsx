/**
 * 邀请面板（管理员用）：生成邀请链接 + 二维码、看已发的邀请、撤销。
 *
 * 用户 2026-10-02 明确要求："不是所有人都能注册，要通过管理员的邀请"。
 * 判据（AGENTS.md §14.1）：管理员不用看文档就知道下一步点哪里 —— 所以这里是
 * **一句话说明 + 一个按钮 + 生成后直接给链接和二维码**，并把"对方打开后要做什么"写在界面上。
 */
import { useState } from 'react';
import { Alert, App as AntApp, Button, Form, Input, InputNumber, Popconfirm, Select, Space, Table, Typography } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { ShareLinkBox } from './ShareLinkBox';
import { ApiError } from '../services/api';
import { invitesApi, type AdminInvite } from '../services/invitesApi';
import { buildInviteUrl } from '../services/publicUrl';

interface CreateForm {
  note?: string;
  maxUses?: number;
  expires?: number;
}

const EXPIRY_OPTIONS = [
  { label: '7 天（推荐）', value: 7 * 24 * 3600 },
  { label: '1 天', value: 24 * 3600 },
  { label: '30 天', value: 30 * 24 * 3600 },
  { label: '永不过期', value: 0 },
];

function statusOf(invite: AdminInvite): { text: string; color: string } {
  if (invite.revoked_at) return { text: '已撤销', color: 'default' };
  if (invite.remaining_uses <= 0) return { text: '已用完', color: 'warning' };
  if (invite.expires_at && new Date(invite.expires_at).getTime() <= Date.now()) {
    return { text: '已过期', color: 'default' };
  }
  return { text: '可用', color: 'green' };
}

function formatTime(value: string | null): string {
  if (!value) return '—';
  return new Date(value).toLocaleString();
}

export function InvitePanel() {
  const { message } = AntApp.useApp();
  const queryClient = useQueryClient();
  const [form] = Form.useForm<CreateForm>();
  const [created, setCreated] = useState<{ url: string; warning?: string } | null>(null);

  const invites = useQuery({ queryKey: ['invites'], queryFn: () => invitesApi.list() });

  const create = useMutation({
    mutationFn: (values: CreateForm) =>
      invitesApi.create({
        note: values.note?.trim() ?? '',
        maxUses: values.maxUses ?? 1,
        ...(values.expires ? { expiresInSeconds: values.expires } : {}),
      }),
    onSuccess: (result) => {
      // 链接用与分享链接同一套"手机上点得开"的规则拼（局域网地址、端口跟随当前页面）
      setCreated(buildInviteUrl(result.token));
      void queryClient.invalidateQueries({ queryKey: ['invites'] });
      form.resetFields();
    },
    onError: (error) => {
      message.error(error instanceof ApiError ? error.message : '生成邀请失败，请稍后重试');
    },
  });

  const revoke = useMutation({
    mutationFn: (inviteId: string) => invitesApi.revoke(inviteId),
    onSuccess: () => {
      message.success('已撤销，这条链接不能再用来注册');
      void queryClient.invalidateQueries({ queryKey: ['invites'] });
    },
    onError: (error) => {
      message.error(error instanceof ApiError ? error.message : '撤销失败，请稍后重试');
    },
  });

  async function handleCopy(text: string) {
    try {
      await navigator.clipboard.writeText(text);
      message.success('已复制到剪贴板');
    } catch {
      message.warning('这个浏览器不允许自动复制，请手动选中链接复制');
    }
  }

  const columns: ColumnsType<AdminInvite> = [
    { title: '备注（给谁用）', dataIndex: 'note', render: (value: string) => value || '—' },
    {
      title: '状态',
      key: 'status',
      render: (_value, invite) => {
        const status = statusOf(invite);
        return <Typography.Text type={status.color === 'green' ? 'success' : 'secondary'}>{status.text}</Typography.Text>;
      },
    },
    {
      title: '已用 / 可用次数',
      key: 'uses',
      render: (_value, invite) => `${invite.used_count} / ${invite.max_uses}`,
    },
    { title: '到期时间', dataIndex: 'expires_at', render: (value: string | null) => formatTime(value) },
    {
      title: '操作',
      key: 'actions',
      render: (_value, invite) => (
        <Popconfirm
          title="撤销这条邀请？"
          description="撤销后对方打开链接会提示无效，需要重新发一条。"
          okText="撤销"
          cancelText="算了"
          onConfirm={() => revoke.mutate(invite.id)}
          disabled={Boolean(invite.revoked_at)}
        >
          <Button danger size="small" disabled={Boolean(invite.revoked_at)}>
            撤销
          </Button>
        </Popconfirm>
      ),
    },
  ];

  return (
    <Space direction="vertical" size="large" style={{ width: '100%' }}>
      <Alert
        type="info"
        showIcon
        message="邀请才能注册"
        description="生成一条邀请链接发给对方（微信/QQ 都行），对方打开后填用户名和邮箱就能注册。一条邀请默认只能用 1 次；用完或撤销后链接立即失效。"
      />

      <Form<CreateForm>
        form={form}
        layout="inline"
        onFinish={(values) => create.mutate(values)}
        initialValues={{ maxUses: 1, expires: 7 * 24 * 3600 }}
      >
        <Form.Item name="note" label="备注">
          <Input placeholder="给小王" style={{ width: 160 }} allowClear />
        </Form.Item>
        <Form.Item name="maxUses" label="可用次数">
          <InputNumber min={1} max={1000} style={{ width: 100 }} />
        </Form.Item>
        <Form.Item name="expires" label="有效期">
          <Select options={EXPIRY_OPTIONS} style={{ width: 140 }} />
        </Form.Item>
        <Form.Item>
          <Button type="primary" htmlType="submit" loading={create.isPending}>
            生成邀请链接
          </Button>
        </Form.Item>
      </Form>

      {created && (
        <div>
          <Typography.Paragraph strong style={{ marginBottom: 8 }}>
            这条链接只显示这一次，请立刻复制或让对方扫码：
          </Typography.Paragraph>
          <ShareLinkBox
            url={created.url}
            warning={created.warning}
            notice="对方打开后填用户名、邮箱、密码即可注册；这条邀请用掉一次就失效（可在下表看到状态）。"
            onCopy={handleCopy}
          />
          <Button style={{ marginTop: 8 }} onClick={() => setCreated(null)}>
            我已复制，收起
          </Button>
        </div>
      )}

      <Table<AdminInvite>
        rowKey="id"
        size="small"
        loading={invites.isLoading}
        dataSource={invites.data ?? []}
        columns={columns}
        pagination={false}
        locale={{ emptyText: '还没有发过邀请' }}
      />
    </Space>
  );
}
