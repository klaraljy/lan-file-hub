import { useState } from 'react';
import { Alert, Button, Card, Form, Input, Space, Typography } from 'antd';
import { Link, useNavigate, useSearchParams } from 'react-router-dom';
import { useQuery } from '@tanstack/react-query';

import { useAuth } from '../features/auth/AuthContext';
import { ApiError } from '../services/api';
import { invitesApi } from '../services/invitesApi';

interface RegisterForm {
  username: string;
  email: string;
  password: string;
  confirm: string;
}

// 与后端 AuthService::ValidateUsername / ValidateEmail 保持一致，
// 但**前端校验只是体验优化**，后端仍然会独立校验（docs/security.md）。
const USERNAME_PATTERN = /^[A-Za-z0-9._-]{3,64}$/;

/**
 * 注册页（含邀请制，用户 2026-10-02 要求：不是所有人都能注册）。
 *
 * 三种情况都要**自己说清楚**（AGENTS.md §14.1「不会用 = 缺陷」）：
 *   1. 允许自助注册、没有邀请 → 正常注册；
 *   2. 带邀请链接（`/register?invite=<token>`）→ 显示"你是被邀请来的"，提交时带上 token；
 *   3. 关闭了自助注册、又没带邀请 → 明确写"本实例需要邀请才能注册，请找管理员要链接"，并禁用注册按钮。
 */
export function RegisterPage() {
  const { register } = useAuth();
  const navigate = useNavigate();
  const [searchParams] = useSearchParams();
  const inviteToken = (searchParams.get('invite') ?? '').trim();

  const [submitting, setSubmitting] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  // 当前实例是否允许自助注册（公开接口，未登录也能问）
  const policy = useQuery({
    queryKey: ['registration-policy'],
    queryFn: () => invitesApi.registrationPolicy(),
    staleTime: 60_000,
  });

  // 带了邀请就校验一次，先告诉用户"这条还能不能用"，别等填完才报错
  const invite = useQuery({
    queryKey: ['invite', inviteToken],
    queryFn: () => invitesApi.describe(inviteToken),
    enabled: Boolean(inviteToken),
    retry: false,
    staleTime: 30_000,
  });

  const selfRegistrationOff = policy.data === false;
  const inviteBlocked = Boolean(inviteToken) && invite.isError;
  const blocked = inviteBlocked || (selfRegistrationOff && !inviteToken);
  const blockedReason = inviteBlocked
    ? '这条邀请链接无效、已过期或已被撤销 —— 请让管理员重新发一条。'
    : '本实例已关闭自助注册，需要管理员发的邀请链接才能注册。';

  async function handleSubmit(values: RegisterForm) {
    setSubmitting(true);
    setErrorMessage(null);
    try {
      await register(
        values.username.trim(),
        values.email.trim(),
        values.password,
        inviteToken || undefined,
      );
      navigate('/files', { replace: true });
    } catch (error) {
      setErrorMessage(error instanceof ApiError ? error.message : '注册失败，请稍后重试');
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div style={{ display: 'flex', justifyContent: 'center', paddingTop: 60 }}>
      <Card style={{ width: 420 }}>
        <Typography.Title level={3} style={{ textAlign: 'center', marginBottom: 24 }}>
          注册账号
        </Typography.Title>

        {inviteToken && invite.isSuccess && (
          <Alert
            type="success"
            showIcon
            style={{ marginBottom: 16 }}
            message="你是被邀请来的"
            description={
              <Space direction="vertical" size={2}>
                <span>{invite.data.note ? `邀请备注：${invite.data.note}` : '管理员给你发了一条邀请。'}</span>
                <span>
                  这条邀请还能用 {invite.data.remaining_uses} 次
                  {invite.data.expires_at
                    ? `，${new Date(invite.data.expires_at).toLocaleString()} 前有效`
                    : '，长期有效'}
                  。
                </span>
              </Space>
            }
          />
        )}

        {blocked && (
          <Alert
            type="warning"
            showIcon
            style={{ marginBottom: 16 }}
            message="现在不能直接注册"
            description={blockedReason}
          />
        )}

        {errorMessage && (
          <Alert type="error" message={errorMessage} showIcon style={{ marginBottom: 16 }} />
        )}

        <Form<RegisterForm>
          layout="vertical"
          onFinish={handleSubmit}
          disabled={submitting || blocked}
        >
          <Form.Item
            label="用户名"
            name="username"
            rules={[
              { required: true, message: '请输入用户名' },
              { pattern: USERNAME_PATTERN, message: '3-64 位，仅限字母、数字、点、下划线和短横线' },
            ]}
          >
            <Input autoComplete="username" autoFocus placeholder="alice" />
          </Form.Item>

          <Form.Item
            label="邮箱"
            name="email"
            rules={[
              { required: true, message: '请输入邮箱' },
              { type: 'email', message: '邮箱格式不正确' },
              { max: 254, message: '邮箱过长' },
            ]}
          >
            <Input autoComplete="email" placeholder="alice@example.com" />
          </Form.Item>

          <Form.Item
            label="密码"
            name="password"
            rules={[
              { required: true, message: '请输入密码' },
              { min: 8, message: '密码至少 8 位' },
              { max: 1024, message: '密码过长' },
            ]}
          >
            <Input.Password autoComplete="new-password" placeholder="至少 8 位" />
          </Form.Item>

          <Form.Item
            label="确认密码"
            name="confirm"
            dependencies={['password']}
            rules={[
              { required: true, message: '请再次输入密码' },
              ({ getFieldValue }) => ({
                validator(_rule, value: string) {
                  if (!value || getFieldValue('password') === value) {
                    return Promise.resolve();
                  }
                  return Promise.reject(new Error('两次输入的密码不一致'));
                },
              }),
            ]}
          >
            <Input.Password autoComplete="new-password" placeholder="再次输入密码" />
          </Form.Item>

          <Button type="primary" htmlType="submit" block loading={submitting} disabled={blocked}>
            注册
          </Button>
        </Form>

        <div style={{ marginTop: 16, textAlign: 'center' }}>
          已有账号？<Link to="/login">去登录</Link>
        </div>
      </Card>
    </div>
  );
}
