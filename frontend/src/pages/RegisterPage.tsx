import { useState } from 'react';
import { Alert, Button, Card, Form, Input, Typography } from 'antd';
import { Link, useNavigate } from 'react-router-dom';

import { useAuth } from '../features/auth/AuthContext';
import { ApiError } from '../services/api';

interface RegisterForm {
  username: string;
  email: string;
  password: string;
  confirm: string;
}

// 与后端 AuthService::ValidateUsername / ValidateEmail 保持一致，
// 但**前端校验只是体验优化**，后端仍然会独立校验（docs/security.md）。
const USERNAME_PATTERN = /^[A-Za-z0-9._-]{3,64}$/;

export function RegisterPage() {
  const { register } = useAuth();
  const navigate = useNavigate();
  const [submitting, setSubmitting] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  async function handleSubmit(values: RegisterForm) {
    setSubmitting(true);
    setErrorMessage(null);
    try {
      await register(values.username.trim(), values.email.trim(), values.password);
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

        {errorMessage && (
          <Alert type="error" message={errorMessage} showIcon style={{ marginBottom: 16 }} />
        )}

        <Form<RegisterForm> layout="vertical" onFinish={handleSubmit} disabled={submitting}>
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

          <Button type="primary" htmlType="submit" block loading={submitting}>
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
