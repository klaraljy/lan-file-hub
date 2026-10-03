/**
 * 回收站页面（docs/frontend.md §2.6）。
 *
 * 支持：查看已删除的文件与文件夹、按类型筛选、恢复、永久删除、显示删除时间与保留期。
 * 恢复与永久删除都必须二次确认；永久删除会说明对象是异步清理的。
 */
import { useState } from 'react';
import {
  Alert,
  App as AntApp,
  Button,
  Card,
  Empty,
  Popconfirm,
  Select,
  Space,
  Spin,
  Table,
  Tag,
  Typography,
} from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { DeleteOutlined, FileOutlined, FolderOutlined, UndoOutlined } from '@ant-design/icons';
import {
  derivePageState,
  pageStateMessage,
  showsContent,
  showsErrorPlaceholder,
  showsSpinner,
} from '../features/files/viewState';
import {
  trashErrorMessage,
  usePurgeFromTrash,
  useRestoreFromTrash,
  useTrash,
} from '../features/trash/useTrash';
import type { TrashItem, TrashItemType } from '../types/trash';
import { MobileCardList, MobileItemCard } from '../components/MobileItemCard';
import { useIsMobile, TOUCH_HEIGHT } from '../hooks/useIsMobile';

function formatBytes(bytes: number): string {
  if (bytes <= 0) return '—';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

export function TrashPage() {
  const { message } = AntApp.useApp();
  const isMobile = useIsMobile();
  const [typeFilter, setTypeFilter] = useState<TrashItemType | undefined>(undefined);
  const [page, setPage] = useState(1);

  const params = { page, page_size: 20, ...(typeFilter ? { type: typeFilter } : {}) };
  const trashQuery = useTrash(params);
  const restore = useRestoreFromTrash();
  const purge = usePurgeFromTrash();

  // 页面状态与上传状态是两套枚举：这里只看这一屏数据的状态
  const state = derivePageState({
    isLoading: trashQuery.isLoading,
    isFetching: trashQuery.isFetching,
    isError: trashQuery.isError,
    error: trashQuery.error,
    hasData: Boolean(trashQuery.data),
  });

  async function handleRestore(item: TrashItem) {
    try {
      const result = await restore.mutateAsync(item.id);
      if (result.already_restored) {
        message.info(`「${item.name}」已经恢复过了`);
      } else if (result.moved_to_root) {
        message.warning(`原目录已不在，「${item.name}」已恢复到「我的文件」根目录`);
      } else {
        message.success(`已恢复「${item.name}」`);
      }
    } catch (error) {
      message.error(trashErrorMessage(error));
    }
  }

  async function handlePurge(item: TrashItem) {
    try {
      const result = await purge.mutateAsync(item.id);
      message.success(
        `已永久删除「${item.name}」，释放 ${formatBytes(result.freed_bytes)}${
          result.queued_objects > 0 ? `（${result.queued_objects} 个对象将在后台清理）` : ''
        }`,
      );
    } catch (error) {
      message.error(trashErrorMessage(error));
    }
  }

  const retentionDays = trashQuery.data?.retention_days ?? 30;

  const columns: ColumnsType<TrashItem> = [
    {
      title: '名称',
      key: 'name',
      render: (_value, item) => (
        <Space>
          {item.type === 'folder' ? (
            <FolderOutlined style={{ color: '#faad14' }} />
          ) : (
            <FileOutlined style={{ color: '#8c8c8c' }} />
          )}
          <span>{item.name}</span>
          <Tag>{item.type === 'folder' ? '文件夹' : '文件'}</Tag>
        </Space>
      ),
    },
    {
      title: '原位置',
      key: 'original',
      width: 180,
      render: (_value, item) => (
        <Typography.Text type="secondary">
          {item.original_folder_name || '我的文件'}
        </Typography.Text>
      ),
    },
    {
      title: '大小',
      key: 'size',
      width: 110,
      render: (_value, item) => formatBytes(item.size_bytes),
    },
    {
      title: '删除时间',
      key: 'deleted_at',
      width: 190,
      render: (_value, item) => new Date(item.deleted_at).toLocaleString(),
    },
    {
      title: '操作',
      key: 'actions',
      width: 200,
      render: (_value, item) => (
        <Space>
          {/* 恢复不是破坏性操作，但仍要确认，避免误点 */}
          <Popconfirm
            title={`恢复「${item.name}」？`}
            description="如果原目录已不存在，会恢复到「我的文件」根目录。"
            okText="恢复"
            cancelText="取消"
            onConfirm={() => void handleRestore(item)}
          >
            <Button type="link" size="small" icon={<UndoOutlined />}>
              恢复
            </Button>
          </Popconfirm>

          {/* 永久删除：不可撤销，措辞必须直白 */}
          <Popconfirm
            title={`永久删除「${item.name}」？`}
            description={`删除后无法找回。${retentionDays} 天内不处理的话，系统也会自动清理。`}
            okText="永久删除"
            okButtonProps={{ danger: true }}
            cancelText="取消"
            onConfirm={() => void handlePurge(item)}
          >
            <Button type="link" size="small" danger icon={<DeleteOutlined />}>
              永久删除
            </Button>
          </Popconfirm>
        </Space>
      ),
    },
  ];


  return (
    <Card title="回收站">
      <Alert
        type="info"
        showIcon
        style={{ marginBottom: 16 }}
        message={`回收站保留 ${retentionDays} 天`}
        description="删除的资源仍占用存储空间；永久删除或超过保留期后，对象会被后台任务清理。"
      />

      <Space style={{ marginBottom: 16 }} wrap>
        <Select
          allowClear
          placeholder="全部类型"
          style={{ width: 160 }}
          value={typeFilter}
          onChange={(value) => {
            setTypeFilter(value);
            setPage(1);
          }}
          options={[
            { value: 'file', label: '仅文件' },
            { value: 'folder', label: '仅文件夹' },
          ]}
        />
        <Button onClick={() => void trashQuery.refetch()} loading={trashQuery.isFetching}>
          刷新
        </Button>
        <Typography.Text type="secondary">共 {trashQuery.data?.total ?? 0} 项</Typography.Text>
      </Space>

      {state === 'idle' && (
        <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={pageStateMessage('idle')} />
      )}

      {showsSpinner(state) && <Spin />}

      {showsErrorPlaceholder(state) && (
        <Empty description={pageStateMessage(state, trashQuery.error)} />
      )}

      {showsContent(state) &&
        (isMobile ? (
          // 手机：一条一张卡（宽表格在手机上表头会竖排、并撑出横向滚动，实测踩到）
          <MobileCardList>
            {(trashQuery.data?.items ?? []).length === 0 ? (
              <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="回收站是空的" />
            ) : (
              (trashQuery.data?.items ?? []).map((item) => (
                <MobileItemCard
                  key={item.id}
                  icon={
                    item.type === 'folder' ? (
                      <FolderOutlined style={{ color: '#faad14' }} />
                    ) : (
                      <FileOutlined style={{ color: '#8c8c8c' }} />
                    )
                  }
                  title={item.name}
                  subtitle={`${item.type === 'folder' ? '文件夹' : formatBytes(item.size_bytes)} · 原位置：${
                    item.original_folder_name || '我的文件'
                  } · ${new Date(item.deleted_at).toLocaleDateString()}`}
                  actions={
                    <>
                      <Popconfirm
                        title={`恢复「${item.name}」？`}
                        okText="恢复"
                        cancelText="取消"
                        onConfirm={() => void handleRestore(item)}
                      >
                        <Button
                          type="primary"
                          icon={<UndoOutlined />}
                          style={{ height: TOUCH_HEIGHT, flex: 1 }}
                        >
                          恢复
                        </Button>
                      </Popconfirm>
                      <Popconfirm
                        title={`永久删除「${item.name}」？`}
                        description="删除后无法找回。"
                        okText="永久删除"
                        okButtonProps={{ danger: true }}
                        cancelText="取消"
                        onConfirm={() => void handlePurge(item)}
                      >
                        <Button danger icon={<DeleteOutlined />} style={{ height: TOUCH_HEIGHT }}>
                          永久删除
                        </Button>
                      </Popconfirm>
                    </>
                  }
                />
              ))
            )}
          </MobileCardList>
        ) : (
          <Table<TrashItem>
            rowKey="id"
            columns={columns}
            dataSource={trashQuery.data?.items ?? []}
            pagination={{
              current: page,
            pageSize: trashQuery.data?.page_size ?? 20,
            total: trashQuery.data?.total ?? 0,
            onChange: setPage,
            showSizeChanger: false,
          }}
          locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="回收站是空的" /> }}
          />
        ))}
    </Card>
  );
}
