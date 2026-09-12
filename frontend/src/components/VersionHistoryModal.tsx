/**
 * 文件版本历史（docs/frontend.md §2.4）。
 *
 * 每行显示版本号、大小、哈希、创建者与时间；可以下载任意历史版本，
 * 也可以恢复到某个版本——**恢复会追加一个新版本，不会删除任何历史**，
 * 所以破坏性操作只需要二次确认，不会造成数据丢失。
 */
import { App as AntApp, Button, Empty, List, Modal, Popconfirm, Space, Spin, Tag, Typography } from 'antd';
import { DownloadOutlined, HistoryOutlined, RollbackOutlined } from '@ant-design/icons';

import {
  conflictCurrentVersion,
  useDownloadVersion,
  useFileVersions,
  useRestoreVersion,
  versionErrorMessage,
} from '../features/versions/useVersions';
import type { FileMeta } from '../types/file';
import type { FileVersion } from '../types/version';

interface Props {
  file: FileMeta | null;
  open: boolean;
  onClose: () => void;
  /** 恢复成功后回调（父组件用它刷新列表或提示） */
  onUpdated?: (file: FileMeta) => void;
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

export function VersionHistoryModal({ file, open, onClose, onUpdated }: Props) {
  const { message } = AntApp.useApp();
  const fileId = file?.id;
  const versions = useFileVersions(fileId, open);
  const downloadVersion = useDownloadVersion();
  const restoreVersion = useRestoreVersion(fileId);

  async function handleDownload(version: FileVersion) {
    if (!file) return;
    try {
      await downloadVersion.mutateAsync({ file, version });
    } catch (error) {
      message.error(versionErrorMessage(error));
    }
  }

  async function handleRestore(version: FileVersion) {
    if (!file) return;
    try {
      const updated = await restoreVersion.mutateAsync({
        version,
        currentVersionId: file.current_version_id,
      });
      message.success(`已把「${file.name}」恢复到 v${version.version_number}（生成了新版本）`);
      onUpdated?.(updated);
    } catch (error) {
      // 版本冲突必须解释清楚：是谁先改了、该怎么办
      const current = conflictCurrentVersion(error);
      if (current) {
        message.warning({
          content: `${versionErrorMessage(error)}（当前版本已更新）`,
          duration: 6,
        });
        void versions.refetch();
        return;
      }
      message.error(versionErrorMessage(error));
    }
  }

  const items = versions.data?.items ?? [];

  return (
    <Modal
      open={open}
      title={
        <Space>
          <HistoryOutlined />
          <span>版本历史{file ? `：${file.name}` : ''}</span>
        </Space>
      }
      footer={null}
      width={720}
      onCancel={onClose}
      destroyOnClose
    >
      {versions.isLoading && <Spin />}
      {versions.error && (
        <Typography.Text type="danger">{versionErrorMessage(versions.error)}</Typography.Text>
      )}
      {!versions.isLoading && !versions.error && items.length === 0 && (
        <Empty description="这个文件还没有版本记录" />
      )}

      {items.length > 0 && (
        <List
          size="small"
          dataSource={items}
          renderItem={(version) => (
            <List.Item
              actions={[
                <Button
                  key="download"
                  type="link"
                  size="small"
                  icon={<DownloadOutlined />}
                  onClick={() => void handleDownload(version)}
                >
                  下载
                </Button>,
                version.is_current ? (
                  <Tag key="current" color="green">
                    当前版本
                  </Tag>
                ) : (
                  <Popconfirm
                    key="restore"
                    title={`恢复到 v${version.version_number}？`}
                    description="恢复会把该版本的内容作为一个**新版本**追加，历史版本不会被删除。"
                    okText="恢复"
                    cancelText="取消"
                    onConfirm={() => void handleRestore(version)}
                  >
                    <Button type="link" size="small" icon={<RollbackOutlined />}>
                      恢复
                    </Button>
                  </Popconfirm>
                ),
              ]}
            >
              <List.Item.Meta
                title={
                  <Space size={8}>
                    <Typography.Text strong>v{version.version_number}</Typography.Text>
                    <Typography.Text type="secondary">{formatBytes(version.size_bytes)}</Typography.Text>
                  </Space>
                }
                description={
                  <Space size={8} wrap>
                    <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                      {new Date(version.created_at).toLocaleString()}
                    </Typography.Text>
                    <Typography.Text code style={{ fontSize: 12 }}>
                      {version.content_hash.slice(0, 12)}…
                    </Typography.Text>
                    <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                      创建者 {version.created_by.slice(0, 8)}…
                    </Typography.Text>
                  </Space>
                }
              />
            </List.Item>
          )}
        />
      )}
    </Modal>
  );
}
