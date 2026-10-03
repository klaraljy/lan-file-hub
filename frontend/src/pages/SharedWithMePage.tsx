/**
 * 「分享给我的」页面（docs/frontend.md §2.2 左侧导航第 3 项）。
 *
 * 展示别人授权给我、或通过分享给我的文件与文件夹：
 *   - 按类型筛选（全部 / 文件 / 文件夹）
 *   - 文件夹可以直接进去浏览（进入后走普通目录页，权限仍由后端判定）
 *   - 文件可以下载（只有 READ 权限也能下载，这是「只读」的含义）
 *   - 显示权限标签，让用户知道自己是只读还是可写
 *
 * 前端只负责展示；真正的权限判定在后端（docs/permissions.md §2）。
 */
import { useMemo, useState } from 'react';
import {
  App as AntApp,
  Button,
  Card,
  Empty,
  Segmented,
  Space,
  Spin,
  Table,
  Tag,
  Typography,
} from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { DownloadOutlined, FileOutlined, FolderOpenOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';

import { formatBytes } from '../features/files/listView';
import { MobileCardList, MobileItemCard } from '../components/MobileItemCard';
import { useIsMobile, TOUCH_HEIGHT } from '../hooks/useIsMobile';
import {
  derivePageState,
  pageStateMessage,
  showsContent,
  showsSpinner,
  showsErrorPlaceholder,
} from '../features/files/viewState';
import { PERMISSION_LABELS, sharingErrorMessage, useSharedWithMe } from '../features/sharing/useSharing';
import { filesApi } from '../services/filesApi';
import type { SharedWithMeFilter, SharedWithMeItem } from '../types/share';

const LEVEL_COLORS: Record<string, string> = {
  READ: 'blue',
  WRITE: 'orange',
  OWNER: 'red',
};

export function SharedWithMePage() {
  const { message } = AntApp.useApp();
  const navigate = useNavigate();
  const isMobile = useIsMobile();
  const [typeFilter, setTypeFilter] = useState<SharedWithMeFilter>('all');
  const [downloading, setDownloading] = useState<string | null>(null);

  const query = useSharedWithMe();

  const items = useMemo(() => {
    const all = query.data?.items ?? [];
    if (typeFilter === 'all') return all;
    return all.filter((item) => item.resource_type === typeFilter);
  }, [query.data, typeFilter]);

  const state = derivePageState({
    isLoading: query.isLoading,
    isFetching: query.isFetching,
    isError: query.isError,
    error: query.error,
    hasData: Boolean(query.data),
  });

  async function handleDownload(item: SharedWithMeItem) {
    setDownloading(item.resource_id);
    try {
      await filesApi.downloadById(item.resource_id, item.name);
    } catch (error) {
      message.error(sharingErrorMessage(error));
    } finally {
      setDownloading(null);
    }
  }

  const columns: ColumnsType<SharedWithMeItem> = [
    {
      title: '名称',
      key: 'name',
      render: (_value, item) => (
        <Space>
          {item.resource_type === 'folder' ? (
            <FolderOpenOutlined style={{ color: '#faad14' }} />
          ) : (
            <FileOutlined style={{ color: '#8c8c8c' }} />
          )}
          {item.resource_type === 'folder' ? (
            <a onClick={() => navigate(`/files/${item.resource_id}`)}>{item.name}</a>
          ) : (
            <span>{item.name}</span>
          )}
        </Space>
      ),
    },
    {
      title: '类型',
      key: 'type',
      width: 110,
      render: (_value, item) => (
        <Tag>{item.resource_type === 'folder' ? '文件夹' : '文件'}</Tag>
      ),
    },
    {
      title: '大小',
      key: 'size',
      width: 110,
      render: (_value, item) =>
        item.resource_type === 'folder' ? '—' : formatBytes(item.size_bytes),
    },
    {
      title: '我的权限',
      key: 'permission',
      width: 200,
      render: (_value, item) => (
        <Space direction="vertical" size={0}>
          <Tag color={LEVEL_COLORS[item.permission] ?? 'default'}>{item.permission}</Tag>
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            {PERMISSION_LABELS[item.permission] ?? '共享访问'}
          </Typography.Text>
        </Space>
      ),
    },
    {
      title: '授权时间',
      key: 'granted_at',
      width: 190,
      render: (_value, item) => new Date(item.granted_at).toLocaleString(),
    },
    {
      title: '操作',
      key: 'actions',
      width: 140,
      render: (_value, item) =>
        item.resource_type === 'folder' ? (
          <Button
            type="link"
            size="small"
            icon={<FolderOpenOutlined />}
            onClick={() => navigate(`/files/${item.resource_id}`)}
          >
            打开
          </Button>
        ) : (
          <Button
            type="link"
            size="small"
            icon={<DownloadOutlined />}
            loading={downloading === item.resource_id}
            onClick={() => void handleDownload(item)}
          >
            下载
          </Button>
        ),
    },
  ];

  return (
    <Card title="分享给我的">
      <Space style={{ marginBottom: 16 }} wrap>
        <Segmented
          value={typeFilter}
          onChange={(value) => setTypeFilter(value as SharedWithMeFilter)}
          options={[
            { value: 'all', label: '全部' },
            { value: 'file', label: '文件' },
            { value: 'folder', label: '文件夹' },
          ]}
        />
        <Button onClick={() => void query.refetch()} loading={query.isFetching}>
          刷新
        </Button>
        <Typography.Text type="secondary">共 {items.length} 项</Typography.Text>
      </Space>

      {showsSpinner(state) && <Spin />}

      {showsErrorPlaceholder(state) && (
        <Empty description={pageStateMessage(state, query.error)} />
      )}

      {showsContent(state) &&
        (isMobile ? (
          // 手机：一条一张卡（宽表格会把表头挤成竖排、并把页面撑出横向滚动，实测踩到）
          <MobileCardList>
            {items.length === 0 ? (
              <Empty
                image={Empty.PRESENTED_IMAGE_SIMPLE}
                description="还没有人分享文件或文件夹给你"
              />
            ) : (
              items.map((item) => (
                <MobileItemCard
                  key={item.resource_id}
                  icon={
                    item.resource_type === 'folder' ? (
                      <FolderOpenOutlined style={{ color: '#faad14' }} />
                    ) : (
                      <FileOutlined style={{ color: '#8c8c8c' }} />
                    )
                  }
                  title={item.name}
                  subtitle={`${
                    item.resource_type === 'folder' ? '文件夹' : formatBytes(item.size_bytes)
                  } · ${PERMISSION_LABELS[item.permission] ?? '共享访问'} · ${new Date(
                    item.granted_at,
                  ).toLocaleDateString()}`}
                  extra={
                    <Tag color={LEVEL_COLORS[item.permission] ?? 'default'} style={{ marginInlineEnd: 0 }}>
                      {item.permission}
                    </Tag>
                  }
                  actions={
                    item.resource_type === 'folder' ? (
                      <Button
                        type="primary"
                        block
                        icon={<FolderOpenOutlined />}
                        style={{ height: TOUCH_HEIGHT }}
                        onClick={() => navigate(`/files/${item.resource_id}`)}
                      >
                        打开文件夹
                      </Button>
                    ) : (
                      <Button
                        type="primary"
                        block
                        icon={<DownloadOutlined />}
                        style={{ height: TOUCH_HEIGHT }}
                        loading={downloading === item.resource_id}
                        onClick={() => void handleDownload(item)}
                      >
                        下载
                      </Button>
                    )
                  }
                />
              ))
            )}
          </MobileCardList>
        ) : (
          <Table<SharedWithMeItem>
            rowKey="resource_id"
            columns={columns}
            dataSource={items}
            pagination={false}
            locale={{
              emptyText: (
                <Empty
                  image={Empty.PRESENTED_IMAGE_SIMPLE}
                  description="还没有人分享文件或文件夹给你"
                />
              ),
            }}
          />
        ))}
    </Card>
  );
}
