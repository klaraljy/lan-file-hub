/**
 * 空间快满了的提醒（2026-10-03，用户批准的功能之一）。
 *
 * 口径（AGENTS §14.1：界面上自己说清楚）：
 *   - ≥80% 温和提醒，并给"去回收站清理"的入口（回收站里的东西照样占配额）；
 *   - ≥95% 严厉提醒（写清"传不进去了"）；
 *   - 未设置配额（0）或用量低于阈值 → **什么都不显示**（不制造焦虑）。
 * 数据来自 `/auth/me` 里的 used_storage_bytes / storage_quota_bytes，不额外请求接口。
 */
import { Alert, Button, Space, Typography } from 'antd';
import { useNavigate } from 'react-router-dom';

import { useAuth } from '../features/auth/AuthContext';
import { formatBytes } from '../features/files/listView';
import { quotaStatus } from '../features/files/limits';

interface Props {
  /** 紧凑模式：手机上省地方（只留一行字 + 一个按钮） */
  compact?: boolean;
}

export function QuotaWarning({ compact = false }: Props) {
  const { user } = useAuth();
  const navigate = useNavigate();
  if (!user) return null;

  const status = quotaStatus(user.used_storage_bytes, user.storage_quota_bytes);
  if (status.level === 'ok') return null;

  const isFull = status.level === 'full';
  const detail = `已用 ${status.percent}%（${formatBytes(status.usedBytes)} / ${formatBytes(
    status.quotaBytes,
  )}）`;

  return (
    <Alert
      type={isFull ? 'error' : 'warning'}
      showIcon
      style={{ marginBottom: 12 }}
      message={
        <Space wrap size={8}>
          <Typography.Text strong>
            {isFull ? '空间几乎满了，新的上传会被拒绝' : '空间快满了'}
          </Typography.Text>
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            {detail}
          </Typography.Text>
        </Space>
      }
      description={
        compact ? undefined : (
          <Typography.Text style={{ fontSize: 12 }}>
            回收站里的文件也算占用空间：去「回收站」把不要的永久删除，或删掉一些旧文件就能腾出来。
          </Typography.Text>
        )
      }
      action={
        <Button size="small" onClick={() => navigate('/trash')}>
          去回收站清理
        </Button>
      }
    />
  );
}
