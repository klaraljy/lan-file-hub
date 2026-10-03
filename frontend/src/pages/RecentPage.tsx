/**
 * 最近文件页（2026-10-03 做成真列表）。
 *
 * 之前这里是一段"需要后端接口"的说明文字 —— 用户看到的是"占位页"，属于缺陷（AGENTS.md §14.1）。
 * 现在后端提供了 `GET /api/v1/recent-files`：跨目录按更新时间倒序列出**自己的**有效文件，
 * 并带上"在哪个文件夹"，所以这一页可以真的用。
 *
 * 口径说明（界面上也写了）：这里只列**自己拥有**的文件；别人分享给你的在「分享给我的」。
 */
import { useMemo, useState } from 'react';
import { App as AntApp, Button, Card, Empty, Space, Spin, Table, Tag, Typography } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { DownloadOutlined, FileOutlined, FolderOpenOutlined } from '@ant-design/icons';
import { useQuery } from '@tanstack/react-query';
import { useNavigate } from 'react-router-dom';

import { MobileCardList, MobileItemCard } from '../components/MobileItemCard';
import { formatBytes, typeLabel } from '../features/files/listView';
import { useIsMobile, TOUCH_HEIGHT } from '../hooks/useIsMobile';
import { filesApi, type RecentFileItem } from '../services/filesApi';

const PAGE_SIZE = 20;

export function RecentPage() {
  const { message } = AntApp.useApp();
  const navigate = useNavigate();
  const isMobile = useIsMobile();
  const [page, setPage] = useState(1);

  const query = useQuery({
    queryKey: ['recent-files', page],
    queryFn: () => filesApi.listRecent({ page, page_size: PAGE_SIZE }),
  });

  const items = useMemo(() => query.data?.items ?? [], [query.data]);

  async function handleDownload(item: RecentFileItem) {
    try {
      await filesApi.downloadById(item.id, item.name);
    } catch {
      message.error('下载失败，请重试');
    }
  }

  const columns: ColumnsType<RecentFileItem> = [
    {
      title: '名称',
      key: 'name',
      render: (_value, item) => (
        <Space>
          <FileOutlined style={{ color: '#8c8c8c' }} />
          <span>{item.name}</span>
        </Space>
      ),
    },
    {
      title: '所在文件夹',
      key: 'folder',
      width: 200,
      render: (_value, item) => (
        <Button type="link" size="small" onClick={() => navigate(`/files/${item.folder_id}`)}>
          {item.folder_name || '我的文件'}
        </Button>
      ),
    },
    {
      title: '大小',
      key: 'size',
      width: 110,
      render: (_value, item) => formatBytes(item.size_bytes),
    },
    {
      title: '类型',
      key: 'type',
      width: 140,
      render: (_value, item) => <Tag>{typeLabel(item)}</Tag>,
    },
    {
      title: '最后修改',
      key: 'updated_at',
      width: 190,
      render: (_value, item) => new Date(item.updated_at).toLocaleString(),
    },
    {
      title: '操作',
      key: 'actions',
      width: 120,
      render: (_value, item) => (
        <Button
          type="link"
          size="small"
          icon={<DownloadOutlined />}
          onClick={() => void handleDownload(item)}
        >
          下载
        </Button>
      ),
    },
  ];

  return (
    <Card title="最近文件">
      <Space direction="vertical" size={12} style={{ width: '100%' }}>
        <Typography.Text type="secondary" style={{ fontSize: 12 }}>
          按最后修改时间倒序，最多显示最近 {PAGE_SIZE} 个（第 {page} 页，共 {query.data?.total ?? 0} 个）。
          这里只列你自己的文件；别人分享给你的在左侧「分享给我的」里。
        </Typography.Text>

        {query.isLoading && <Spin />}

        {query.isError && (
          <Empty description="读取最近文件失败，请点下面的刷新重试">
            <Button onClick={() => void query.refetch()}>刷新</Button>
          </Empty>
        )}

        {query.isSuccess &&
          (items.length === 0 ? (
            <Empty
              image={Empty.PRESENTED_IMAGE_SIMPLE}
              description="还没有文件。去「我的文件」上传一个，它就会出现在这里"
            >
              <Button type="primary" onClick={() => navigate('/files')}>
                去我的文件
              </Button>
            </Empty>
          ) : isMobile ? (
            <MobileCardList>
              {items.map((item) => (
                <MobileItemCard
                  key={item.id}
                  icon={<FileOutlined style={{ color: '#8c8c8c' }} />}
                  title={item.name}
                  subtitle={`${formatBytes(item.size_bytes)} · 在「${
                    item.folder_name || '我的文件'
                  }」 · ${new Date(item.updated_at).toLocaleDateString()}`}
                  actions={
                    <>
                      <Button
                        type="primary"
                        icon={<DownloadOutlined />}
                        style={{ height: TOUCH_HEIGHT, flex: 1 }}
                        onClick={() => void handleDownload(item)}
                      >
                        下载
                      </Button>
                      <Button
                        icon={<FolderOpenOutlined />}
                        style={{ height: TOUCH_HEIGHT }}
                        onClick={() => navigate(`/files/${item.folder_id}`)}
                      >
                        去文件夹
                      </Button>
                    </>
                  }
                />
              ))}
            </MobileCardList>
          ) : (
            <Table<RecentFileItem>
              rowKey="id"
              columns={columns}
              dataSource={items}
              scroll={{ x: 720 }}
              pagination={{
                current: page,
                pageSize: PAGE_SIZE,
                total: query.data?.total ?? 0,
                onChange: setPage,
                showSizeChanger: false,
              }}
            />
          ))}
      </Space>
    </Card>
  );
}
