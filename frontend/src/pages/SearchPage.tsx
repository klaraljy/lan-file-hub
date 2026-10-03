/**
 * 搜索页（2026-10-03，用户要求：文件一多就得能找到东西）。
 *
 * 界面口径（AGENTS.md §14.1，不许让用户猜）：
 *   - 输入框旁边就写清"搜的是你自己的文件和文件夹"；
 *   - 没输入时给一句"输入关键字开始搜索"，而不是空白页；
 *   - 搜不到时告诉用户"换个词试试"，并给一个回「我的文件」的出口；
 *   - 结果里**显示文件在哪个文件夹**（只给文件名等于让用户自己找）。
 */
import { useEffect, useMemo, useState } from 'react';
import { App as AntApp, Button, Card, Empty, Input, List, Space, Spin, Tag, Typography } from 'antd';
import {
  DownloadOutlined,
  EyeOutlined,
  FileImageOutlined,
  FileOutlined,
  FolderOpenOutlined,
  SearchOutlined,
} from '@ant-design/icons';
import { useQuery } from '@tanstack/react-query';
import { useNavigate, useSearchParams } from 'react-router-dom';

import { FilePreviewModal } from '../components/FilePreviewModal';
import { FileThumbnail } from '../components/FileThumbnail';
import { MobileCardList, MobileItemCard } from '../components/MobileItemCard';
import { formatBytes, typeLabel } from '../features/files/listView';
import { useIsMobile, TOUCH_HEIGHT } from '../hooks/useIsMobile';
import { filesApi } from '../services/filesApi';
import { canPreview } from '../services/filePreview';
import { searchFiles, type SearchFile } from '../services/searchApi';

const PAGE_SIZE = 20;

export function SearchPage() {
  const { message } = AntApp.useApp();
  const navigate = useNavigate();
  const isMobile = useIsMobile();
  const [params, setParams] = useSearchParams();
  const [term, setTerm] = useState(params.get('q') ?? '');
  const [page, setPage] = useState(1);
  const [previewTarget, setPreviewTarget] = useState<SearchFile | null>(null);

  const activeQuery = (params.get('q') ?? '').trim();

  // 地址栏里的 q 变了（返回/前进、或从我的文件跳过来）就同步输入框
  useEffect(() => {
    setTerm(params.get('q') ?? '');
    setPage(1);
  }, [params]);

  const query = useQuery({
    queryKey: ['search', activeQuery, page],
    queryFn: () => searchFiles(activeQuery, page, PAGE_SIZE),
    enabled: activeQuery.length > 0,
    // 搜索不做缓存：用户改一个字就该看到新结果
    staleTime: 0,
  });

  const items = useMemo(() => query.data?.items ?? [], [query.data]);
  const folders = useMemo(() => query.data?.folders ?? [], [query.data]);

  function submit(value: string) {
    const trimmed = value.trim();
    setPage(1);
    setParams(trimmed ? { q: trimmed } : {});
  }

  async function handleDownload(item: SearchFile) {
    try {
      await filesApi.downloadById(item.id, item.name);
    } catch {
      message.error('下载失败，请重试');
    }
  }

  return (
    <Card title="搜索">
      <Space direction="vertical" size={12} style={{ width: '100%' }}>
        <Input.Search
          allowClear
          size="large"
          value={term}
          placeholder="输入文件名或文件夹名，例如：报表"
          enterButton={
            <>
              <SearchOutlined /> 搜索
            </>
          }
          onChange={(event) => setTerm(event.target.value)}
          onSearch={submit}
        />
        <Typography.Text type="secondary" style={{ fontSize: 12 }}>
          搜的是**你自己的**文件和文件夹（回收站里的不算）；别人分享给你的在左侧「分享给我的」。
        </Typography.Text>

        {activeQuery.length === 0 && (
          <Empty
            image={Empty.PRESENTED_IMAGE_SIMPLE}
            description="输入关键字开始搜索；文件名的一部分就能搜到"
          >
            <Button type="primary" onClick={() => navigate('/files')}>
              去我的文件
            </Button>
          </Empty>
        )}

        {activeQuery.length > 0 && query.isLoading && <Spin />}

        {activeQuery.length > 0 && query.isError && (
          <Empty description="搜索失败，请点下面的刷新重试">
            <Button onClick={() => void query.refetch()}>重试</Button>
          </Empty>
        )}

        {activeQuery.length > 0 && query.isSuccess && (
          <>
            <Typography.Text type="secondary" style={{ fontSize: 12 }}>
              关键字「{activeQuery}」：找到 {query.data?.total ?? 0} 个文件
              {folders.length > 0 ? `、${folders.length} 个文件夹` : ''}。
            </Typography.Text>

            {folders.length > 0 && (
              <Space wrap>
                {folders.map((folder) => (
                  <Tag
                    key={folder.id}
                    icon={<FolderOpenOutlined />}
                    style={{ cursor: 'pointer', padding: '4px 10px', fontSize: 14 }}
                    onClick={() => navigate(`/files/${folder.id}`)}
                  >
                    {folder.name}
                  </Tag>
                ))}
              </Space>
            )}

            {items.length === 0 && folders.length === 0 && (
              <Empty
                image={Empty.PRESENTED_IMAGE_SIMPLE}
                description={`没找到和「${activeQuery}」有关的文件，换个词试试`}
              >
                <Button onClick={() => navigate('/files')}>去我的文件</Button>
              </Empty>
            )}

            {isMobile ? (
              <MobileCardList>
                {items.map((item) => (
                  <MobileItemCard
                    key={item.id}
                    icon={
                      <FileThumbnail
                        fileId={item.id}
                        fileName={item.name}
                        mimeType={item.mime_type}
                        size={40}
                      />
                    }
                    title={item.name}
                    subtitle={`${formatBytes(item.size_bytes)} · 在「${
                      item.folder_name || '我的文件'
                    }」`}
                    actions={
                      <>
                        {canPreview(item.mime_type, item.name) ? (
                          <Button
                            type="primary"
                            icon={<EyeOutlined />}
                            style={{ height: TOUCH_HEIGHT, flex: 1 }}
                            onClick={() => setPreviewTarget(item)}
                          >
                            查看
                          </Button>
                        ) : (
                          <Button
                            type="primary"
                            icon={<DownloadOutlined />}
                            style={{ height: TOUCH_HEIGHT, flex: 1 }}
                            onClick={() => void handleDownload(item)}
                          >
                            下载
                          </Button>
                        )}
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
              <List
                dataSource={items}
                pagination={
                  (query.data?.total ?? 0) > PAGE_SIZE
                    ? {
                        current: page,
                        pageSize: PAGE_SIZE,
                        total: query.data?.total ?? 0,
                        onChange: setPage,
                        showSizeChanger: false,
                      }
                    : false
                }
                renderItem={(item) => (
                  <List.Item
                    actions={[
                      canPreview(item.mime_type, item.name) ? (
                        <Button
                          key="preview"
                          type="link"
                          icon={<EyeOutlined />}
                          onClick={() => setPreviewTarget(item)}
                        >
                          查看
                        </Button>
                      ) : null,
                      <Button
                        key="download"
                        type="link"
                        icon={<DownloadOutlined />}
                        onClick={() => void handleDownload(item)}
                      >
                        下载
                      </Button>,
                      <Button
                        key="folder"
                        type="link"
                        icon={<FolderOpenOutlined />}
                        onClick={() => navigate(`/files/${item.folder_id}`)}
                      >
                        去文件夹
                      </Button>,
                    ].filter(Boolean)}
                  >
                    <List.Item.Meta
                      avatar={
                        <FileThumbnail
                          fileId={item.id}
                          fileName={item.name}
                          mimeType={item.mime_type}
                          size={44}
                        />
                      }
                      title={item.name}
                      description={
                        <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                          {formatBytes(item.size_bytes)} · {typeLabel(item)} · 在「
                          {item.folder_name || '我的文件'}」
                          {canPreview(item.mime_type, item.name) ? (
                            <>
                              {' '}
                              <FileImageOutlined />
                            </>
                          ) : (
                            <>
                              {' '}
                              <FileOutlined />
                            </>
                          )}
                        </Typography.Text>
                      }
                    />
                  </List.Item>
                )}
              />
            )}
          </>
        )}
      </Space>

      <FilePreviewModal
        open={previewTarget !== null}
        fileId={previewTarget?.id ?? ''}
        fileName={previewTarget?.name ?? ''}
        mimeType={previewTarget?.mime_type}
        onDownload={() => {
          if (previewTarget) void handleDownload(previewTarget);
        }}
        onClose={() => setPreviewTarget(null)}
      />
    </Card>
  );
}
