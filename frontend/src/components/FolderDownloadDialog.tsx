/**
 * 「下载文件夹里的东西」弹窗（2026-10-03 用户要求）。
 *
 * 用户原话：「我是想一同下载文件夹里面的东西，而不是打包成 zip」
 *          「下载的时候还可以显示一下内容」
 *
 * 所以这里做两件事：
 *   ① 先把这个目录（含子目录）里有什么**列出来**：多少个文件、多大、都有哪些；
 *   ② 用户按「开始下载」后，**逐个**走既有的"一次性票据 + 浏览器原生下载" —— 不打包、不占额外磁盘。
 *      浏览器会问一次「是否允许下载多个文件」，界面上先说明。
 */
import { useMemo, useState } from 'react';
import { Alert, App as AntApp, Button, Empty, List, Space, Spin, Typography } from 'antd';
import { DownloadOutlined } from '@ant-design/icons';
import { useQuery } from '@tanstack/react-query';

import { ResponsiveSheet } from './ResponsiveSheet';
import {
  batchDoneText,
  batchProgressText,
  downloadSequentially,
  type BatchDownloadItem,
} from '../features/files/batchDownload';
import { formatBytes } from '../features/files/listView';
import { filesApi } from '../services/filesApi';
import type { Folder } from '../types/folder';

interface Props {
  folder: Folder | null;
  onClose: () => void;
  /** 下载完成后通知外面（例如刷新一下） */
  onFinished?: () => void;
}

/** 清单里最多显示这么多条（再多也没人翻，但下载的还是全部） */
const PREVIEW_LIMIT = 200;

export function FolderDownloadDialog({ folder, onClose, onFinished }: Props) {
  const { message } = AntApp.useApp();
  const [progress, setProgress] = useState('');
  const [running, setRunning] = useState(false);

  const listing = useQuery({
    queryKey: ['folder-files', folder?.id ?? ''],
    queryFn: () => filesApi.listFolderFiles(folder!.id),
    enabled: Boolean(folder?.id),
  });

  const items: BatchDownloadItem[] = useMemo(
    () =>
      (listing.data?.items ?? []).map((entry) => ({
        id: entry.file_id,
        name: entry.name,
        sizeBytes: entry.size_bytes,
      })),
    [listing.data],
  );

  async function startDownload() {
    if (items.length === 0) return;
    setRunning(true);
    setProgress(batchProgressText(0, items.length));
    const result = await downloadSequentially(
      items,
      async (item) => {
        await filesApi.downloadById(item.id, item.name);
      },
      { onProgress: (done, total) => setProgress(batchProgressText(done, total)) },
    );
    setRunning(false);
    setProgress('');
    if (result.failed === 0) {
      message.success(batchDoneText(result.done, 0));
    } else {
      message.warning(batchDoneText(result.done, result.failed));
    }
    onFinished?.();
    onClose();
  }

  const total = listing.data?.file_count ?? 0;
  const preview = (listing.data?.items ?? []).slice(0, PREVIEW_LIMIT);

  return (
    <ResponsiveSheet
      open={folder !== null}
      onClose={onClose}
      desktopWidth={640}
      mobileHeight="85%"
      title={`下载「${folder?.name ?? ''}」里的文件`}
      footer={
        <Button
          type="primary"
          block
          size="large"
          icon={<DownloadOutlined />}
          loading={running}
          disabled={total === 0 || listing.isLoading}
          onClick={() => void startDownload()}
        >
          {total > 1 ? `开始下载（${total} 个文件）` : '开始下载'}
        </Button>
      }
    >
      {listing.isLoading && <Spin />}

      {listing.isError && (
        <Alert type="warning" showIcon message="读不到这个文件夹里的文件清单，请稍后再试" />
      )}

      {listing.data && (
        <Space direction="vertical" size={10} style={{ width: '100%' }}>
          <Alert
            type="info"
            showIcon
            message={
              <span>
                里面有 <b>{listing.data.file_count}</b> 个文件，共约{' '}
                <b>{formatBytes(listing.data.total_bytes)}</b>
              </span>
            }
            description="按下「开始下载」后会一个一个传到你的设备上（不打包成压缩包）；浏览器可能问一次「是否允许下载多个文件」，点允许即可。"
          />

          {listing.data.truncated && (
            <Alert
              type="warning"
              showIcon
              message={`文件太多，这里只列了前 ${listing.data.items.length} 个；按「开始下载」下的还是全部 ${listing.data.file_count} 个`}
            />
          )}

          {progress && (
            <Typography.Text type="secondary">{progress}</Typography.Text>
          )}

          {preview.length === 0 ? (
            <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="这个文件夹里没有文件" />
          ) : (
            <List
              size="small"
              dataSource={preview}
              style={{ maxHeight: '45vh', overflow: 'auto' }}
              renderItem={(entry) => (
                <List.Item>
                  <Typography.Text ellipsis style={{ minWidth: 0 }}>
                    {entry.path}
                  </Typography.Text>
                  <Typography.Text type="secondary" style={{ whiteSpace: 'nowrap' }}>
                    {formatBytes(entry.size_bytes)}
                  </Typography.Text>
                </List.Item>
              )}
            />
          )}
        </Space>
      )}
    </ResponsiveSheet>
  );
}
