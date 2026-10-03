import { useState } from 'react';
import { Alert, Button, Card, Checkbox, Form, Input, Typography } from 'antd';
import { Link, useLocation, useNavigate } from 'react-router-dom';

import { useAuth } from '../features/auth/AuthContext';
import { ApiError } from '../services/api';

interface LoginForm {
  identifier: string;
  password: string;
  /** 「记住我」（2026-10-03）：默认勾上 —— 手机 App 里每次都要重登是产品缺陷 */
  remember: boolean;
}

export function LoginPage() {
  const { login } = useAuth();
  const navigate = useNavigate();
  const location = useLocation();
  const [submitting, setSubmitting] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  // 从受保护页面跳过来时，登录成功后回到原处
  const from = (location.state as { from?: string } | null)?.from ?? '/files';

  async function handleSubmit(values: LoginForm) {
    setSubmitting(true);
    setErrorMessage(null);
    try {
      await login(values.identifier.trim(), values.password, values.remember);
      navigate(from, { replace: true });
    } catch (error) {
      // 后端对「用户不存在」和「密码错误」返回同一句文案，这里原样展示
      setErrorMessage(error instanceof ApiError ? error.message : '登录失败，请稍后重试');
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div style={{ display: 'flex', justifyContent: 'center', paddingTop: 80 }}>
      <Card style={{ width: 400 }}>
        <Typography.Title level={3} style={{ textAlign: 'center', marginBottom: 24 }}>
          lan-file-hub
        </Typography.Title>

        {errorMessage && (
          <Alert type="error" message={errorMessage} showIcon style={{ marginBottom: 16 }} />
        )}

        <Form<LoginForm> layout="vertical" onFinish={handleSubmit} disabled={submitting}>
          <Form.Item
            label="用户名或邮箱"
            name="identifier"
            rules={[{ required: true, message: '请输入用户名或邮箱' }]}
          >
            <Input autoComplete="username" autoFocus placeholder="alice 或 alice@example.com" />
          </Form.Item>

          <Form.Item label="密码" name="password" rules={[{ required: true, message: '请输入密码' }]}>
            {/* 密码不写入任何本地存储；浏览器自动填充由 autoComplete 控制 */}
            <Input.Password autoComplete="current-password" placeholder="请输入密码" />
          </Form.Item>

          <Form.Item name="remember" valuePropName="checked" initialValue={true}>
            <Checkbox>
              记住我
              <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                （这台设备 30 天内不用再登录；公用电脑请取消勾选）
              </Typography.Text>
            </Checkbox>
          </Form.Item>

          <Button type="primary" htmlType="submit" block loading={submitting}>
            登录
          </Button>
        </Form>

        <div style={{ marginTop: 16, textAlign: 'center' }}>
          还没有账号？<Link to="/register">立即注册</Link>
        </div>
      </Card>
    </div>
  );
}
