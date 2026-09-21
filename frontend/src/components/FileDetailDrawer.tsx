/**
 * 文件详情抽屉（docs/frontend.md §2.4）。
 *
 * 展示文件名、类型、大小、SHA-256、当前版本、创建者、时间与状态，
 * 并作为「版本历史 / 分享 / 权限」三个弹窗的入口。
 *
 * 抽屉本身不拥有那些弹窗：由父组件（FilesPage）继续持有弹窗状态，
 * 这样从列表行和从抽屉两个入口打开的是同一套弹窗，不会出现两份状态。
 */
import { Button, Descriptions, Drawer, Space, Tag, Typography } from 'antd';
import {
  HistoryOutlined,
  SendOutlined,
  ShareAltOutlined,
  TeamOutlined,
} from '@ant-design/icons';

import { formatBytes, typeLabel } from '../features/files/listView';
import type { FileMeta } from '../types/file';

interface Props {
  file: FileMeta | null;
  open: boolean;
  onClose: () => void;
  onOpenVersions: (file: FileMeta) => void;
  onOpenShare: (file: FileMeta) => void;
  onOpenPermissions: (file: FileMeta) => void;
  /** 「发送给…」：按人发送（AGENTS.md §14.3） */
  onOpenSend: (file: FileMeta) => void;
}

const STATUS_TEXT: Record<FileMeta['status'], { text: string; color: string }> = {
  ACTIVE: { text: '正常', color: 'green' },
  TRASHED: { text: '回收站中', color: 'orange' },
  PURGED: { text: '已永久删除', color: 'red' },
};

export function FileDetailDrawer({
  file,
  open,
  onClose,
  onOpenVersions,
  onOpenShare,
  onOpenPermissions,
  onOpenSend,
}: Props) {
  const status = file ? STATUS_TEXT[file.status] : null;

  return (
    <Drawer
      open={open}
      title={file ? `文件详情：${file.name}` : '文件详情'}
      width={520}
      onClose={onClose}
      destroyOnClose
    >
      {file && (
        <Space direction="vertical" size="middle" style={{ width: '100%' }}>
          <Descriptions column={1} size="small" bordered>
            <Descriptions.Item label="文件名">{file.name}</Descriptions.Item>
            <Descriptions.Item label="类型">{typeLabel(file)}</Descriptions.Item>
            <Descriptions.Item label="大小">{formatBytes(file.size_bytes)}</Descriptions.Item>
            <Descriptions.Item label="SHA-256">
              <Typography.Text code copyable style={{ fontSize: 12, wordBreak: 'break-all' }}>
                {file.content_hash}
              </Typography.Text>
            </Descriptions.Item>
            <Descriptions.Item label="当前版本">
              {file.current_version_id ? (
                <Typography.Text code style={{ fontSize: 12 }}>
                  {file.current_version_id}
                </Typography.Text>
              ) : (
                '—'
              )}
            </Descriptions.Item>
            <Descriptions.Item label="所有者">
              <Typography.Text code style={{ fontSize: 12 }}>
                {file.owner_id}
              </Typography.Text>
            </Descriptions.Item>
            <Descriptions.Item label="创建时间">
              {new Date(file.created_at).toLocaleString()}
            </Descriptions.Item>
            <Descriptions.Item label="修改时间">
              {new Date(file.updated_at).toLocaleString()}
            </Descriptions.Item>
            <Descriptions.Item label="状态">
              {status ? <Tag color={status.color}>{status.text}</Tag> : '—'}
              {file.deleted_at && (
                <Typography.Text type="secondary" style={{ marginLeft: 8, fontSize: 12 }}>
                  删除于 {new Date(file.deleted_at).toLocaleString()}
                </Typography.Text>
              )}
            </Descriptions.Item>
          </Descriptions>

          <Space wrap>
            {/* 主操作：把文件发给某个人（按用户名/邮箱选人） */}
            <Button type="primary" icon={<SendOutlined />} onClick={() => onOpenSend(file)}>
              发送给…
            </Button>
            <Button icon={<HistoryOutlined />} onClick={() => onOpenVersions(file)}>
              版本历史
            </Button>
            <Button icon={<ShareAltOutlined />} onClick={() => onOpenShare(file)}>
              分享
            </Button>
            <Button icon={<TeamOutlined />} onClick={() => onOpenPermissions(file)}>
              权限
            </Button>
          </Space>

          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            前端展示的这些信息全部来自后端；下载与分享的实际权限每次都由后端重新判定。
          </Typography.Text>
        </Space>
      )}
    </Drawer>
  );
}
