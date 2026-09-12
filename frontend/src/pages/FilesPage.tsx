/**
 * 我的文件（docs/frontend.md §2.2、§2.3）。
 *
 * 职责：目录浏览（列表/网格两种视图）、排序与名称过滤、上传面板、
 * 文件夹增删改、文件下载/详情/版本/分享/权限/删除，以及目录级的状态展示。
 *
 * 说明两点边界：
 *   1. 排序与搜索都是**针对当前已加载的这一页**做的前端计算：列表接口目前按目录分页返回，
 *      服务端排序白名单与跨目录全文搜索需要后端参数支持（docs/api.md §分页与排序）。
 *   2. 页面状态与上传状态是两套枚举（docs/frontend.md §4.1/§4.2），
 *      目录区用 viewState 推导页面状态，上传面板自己管上传状态，互不顶替。
 */
import { useEffect, useMemo, useState } from 'react';
import {
  App as AntApp,
  Button,
  Card,
  Dropdown,
  Empty,
  Form,
  Input,
  Modal,
  Popconfirm,
  Segmented,
  Space,
  Spin,
  Table,
  Tag,
  Typography,
} from 'antd';
import type { ColumnsType } from 'antd/es/table';
import {
  AppstoreOutlined,
  DeleteOutlined,
  DownloadOutlined,
  EditOutlined,
  FileOutlined,
  FolderOutlined,
  HistoryOutlined,
  InfoCircleOutlined,
  PlusOutlined,
  ReloadOutlined,
  ShareAltOutlined,
  SortAscendingOutlined,
  TeamOutlined,
  UnorderedListOutlined,
} from '@ant-design/icons';
import { useNavigate, useParams } from 'react-router-dom';

import { FileDetailDrawer } from '../components/FileDetailDrawer';
import { FileGrid } from '../components/FileGrid';
import { FolderBreadcrumb } from '../components/FolderBreadcrumb';
import { PermissionDialog } from '../components/PermissionDialog';
import { ShareDialog } from '../components/ShareDialog';
import { UploadPanel } from '../components/UploadPanel';
import { VersionHistoryModal } from '../components/VersionHistoryModal';
import { useAuth } from '../features/auth/AuthContext';
import { fileErrorMessage, useDownloadFile, useFolderFiles } from '../features/files/useFiles';
import {
  DEFAULT_SORT,
  SORT_FIELDS,
  SORT_FIELD_LABELS,
  buildBrowseRows,
  filterRows,
  formatBytes,
  nextSortState,
  sortIndicator,
  sortRows,
  typeLabel,
  type BrowseRow,
  type SortField,
  type SortState,
  type ViewMode,
} from '../features/files/listView';
import {
  derivePageState,
  pageStateMessage,
  showsContent,
  showsErrorPlaceholder,
  showsSpinner,
} from '../features/files/viewState';
import {
  folderErrorMessage,
  useCreateFolder,
  useDeleteFolder,
  useFolderChildren,
  useFolderPath,
  useRenameFolder,
  useRootFolder,
} from '../features/folders/useFolders';
import { deleteFileApi } from '../services/trashApi';
import type { FileMeta } from '../types/file';
import type { Folder } from '../types/folder';

type EditorMode = { kind: 'create' } | { kind: 'rename'; folder: Folder } | null;
type ShareTarget = { file?: FileMeta; folder?: Folder } | null;

export function FilesPage() {
  const { user } = useAuth();
  const { message } = AntApp.useApp();
  const navigate = useNavigate();
  const params = useParams<{ folderId?: string }>();

  const [editor, setEditor] = useState<EditorMode>(null);
  const [versionTarget, setVersionTarget] = useState<FileMeta | null>(null);
  const [detailTarget, setDetailTarget] = useState<FileMeta | null>(null);
  const [shareTarget, setShareTarget] = useState<ShareTarget>(null);
  const [permTarget, setPermTarget] = useState<ShareTarget>(null);
  const [sort, setSort] = useState<SortState>(DEFAULT_SORT);
  const [viewMode, setViewMode] = useState<ViewMode>('list');
  const [keyword, setKeyword] = useState('');
  const [form] = Form.useForm<{ name: string }>();

  const rootQuery = useRootFolder();
  // 第一次进入没有 folderId 时跳到根目录，让 URL 与左侧导航保持一致
  const currentId = params.folderId ?? rootQuery.data?.id;

  useEffect(() => {
    if (!params.folderId && rootQuery.data) {
      navigate(`/files/${rootQuery.data.id}`, { replace: true });
    }
  }, [params.folderId, rootQuery.data, navigate]);

  const pathQuery = useFolderPath(currentId);
  // 目录里同时有子目录和文件，两者是两个接口，这里合并展示
  const foldersQuery = useFolderChildren(currentId, { sort_by: 'name', sort_order: 'asc' });
  const childrenQuery = useFolderFiles(currentId, { sort_by: 'name', sort_order: 'asc' });

  const createMutation = useCreateFolder();
  const renameMutation = useRenameFolder();
  const deleteMutation = useDeleteFolder();
  const downloadMutation = useDownloadFile();

  const chain = pathQuery.data ?? [];
  const currentFolder = chain.length > 0 ? chain[chain.length - 1] : undefined;
  const currentLabel = useMemo(() => {
    if (!currentFolder) return '';
    return currentFolder.is_root ? '我的文件' : currentFolder.name;
  }, [currentFolder]);

  /** 合并 → 过滤 → 排序，全部是纯函数（features/files/listView.ts） */
  const rows: BrowseRow[] = useMemo(() => {
    const merged = buildBrowseRows(
      foldersQuery.data?.items ?? [],
      childrenQuery.data?.items ?? [],
    );
    return sortRows(filterRows(merged, keyword), sort);
  }, [foldersQuery.data, childrenQuery.data, keyword, sort]);

  const state = derivePageState({
    isLoading: rootQuery.isLoading || pathQuery.isLoading,
    isFetching:
      foldersQuery.isFetching || childrenQuery.isFetching || rootQuery.isFetching || pathQuery.isFetching,
    isError: Boolean(rootQuery.error ?? pathQuery.error ?? foldersQuery.error ?? childrenQuery.error),
    error: rootQuery.error ?? pathQuery.error ?? foldersQuery.error ?? childrenQuery.error,
    hasData: Boolean(foldersQuery.data && childrenQuery.data),
  });

  function openCreate() {
    form.setFieldsValue({ name: '' });
    setEditor({ kind: 'create' });
  }

  function openRename(folder: Folder) {
    form.setFieldsValue({ name: folder.name });
    setEditor({ kind: 'rename', folder });
  }

  async function submitEditor() {
    const values = await form.validateFields();
    const name = values.name.trim();
    if (!name) {
      message.error('文件夹名称不能为空');
      return;
    }
    try {
      if (editor?.kind === 'create') {
        if (!currentId) return;
        await createMutation.mutateAsync({ parentId: currentId, name });
        message.success('文件夹已创建');
      } else if (editor?.kind === 'rename') {
        await renameMutation.mutateAsync({ folderId: editor.folder.id, name });
        message.success('已重命名');
      }
      setEditor(null);
    } catch (error) {
      message.error(folderErrorMessage(error));
    }
  }

  async function handleDeleteFolder(folder: Folder) {
    try {
      await deleteMutation.mutateAsync(folder.id);
      message.success(`已删除「${folder.name}」到回收站`);
    } catch (error) {
      message.error(folderErrorMessage(error));
    }
  }

  async function handleDownload(file: FileMeta) {
    try {
      await downloadMutation.mutateAsync(file);
    } catch (error) {
      message.error(fileErrorMessage(error));
    }
  }

  // 软删除：文件进回收站，可恢复（阶段八）
  async function handleDeleteFile(file: FileMeta) {
    try {
      await deleteFileApi.softDelete(file.id);
      message.success(`已将「${file.name}」移入回收站`);
      await childrenQuery.refetch();
    } catch (error) {
      message.error(fileErrorMessage(error));
    }
  }

  /** 网格视图传来的行：按 kind 分派到具体操作 */
  function handleRowDelete(row: BrowseRow) {
    if (row.kind === 'file') {
      void handleDeleteFile(row.file);
    } else {
      void handleDeleteFolder(row.folder);
    }
  }

  const columns: ColumnsType<BrowseRow> = [
    {
      title: '名称',
      key: 'name',
      render: (_value, row) => {
        if (row.kind === 'folder') {
          return (
            <Space>
              <FolderOutlined style={{ color: '#faad14' }} />
              <a onClick={() => navigate(`/files/${row.folder.id}`)}>{row.folder.name}</a>
            </Space>
          );
        }
        return (
          <Space>
            <FileOutlined style={{ color: '#8c8c8c' }} />
            <span>{row.file.name}</span>
          </Space>
        );
      },
    },
    {
      title: '类型',
      key: 'type',
      width: 150,
      render: (_value, row) =>
        row.kind === 'folder' ? <Tag>文件夹</Tag> : <Tag color="blue">{typeLabel(row.file)}</Tag>,
    },
    {
      title: '大小',
      key: 'size',
      width: 110,
      render: (_value, row) => (row.kind === 'folder' ? '—' : formatBytes(row.file.size_bytes)),
    },
    {
      title: '修改时间',
      key: 'updated_at',
      width: 190,
      render: (_value, row) => new Date(row.updated_at).toLocaleString(),
    },
    {
      title: '操作',
      key: 'actions',
      width: 260,
      render: (_value, row) => {
        if (row.kind === 'file') {
          return (
            <Space wrap size={0}>
              <Button
                type="link"
                size="small"
                icon={<DownloadOutlined />}
                loading={downloadMutation.isPending}
                onClick={() => void handleDownload(row.file)}
              >
                下载
              </Button>
              <Button
                type="link"
                size="small"
                icon={<InfoCircleOutlined />}
                onClick={() => setDetailTarget(row.file)}
              >
                详情
              </Button>
              <Button
                type="link"
                size="small"
                icon={<HistoryOutlined />}
                onClick={() => setVersionTarget(row.file)}
              >
                版本历史
              </Button>
              <Button
                type="link"
                size="small"
                icon={<ShareAltOutlined />}
                onClick={() => setShareTarget({ file: row.file })}
              >
                分享
              </Button>
              <Button
                type="link"
                size="small"
                icon={<TeamOutlined />}
                onClick={() => setPermTarget({ file: row.file })}
              >
                权限
              </Button>
              {/* 软删除：进回收站，可恢复 */}
              <Popconfirm
                title={`删除「${row.file.name}」？`}
                description="文件会移入回收站，可以恢复。"
                okText="删除"
                okButtonProps={{ danger: true }}
                cancelText="取消"
                onConfirm={() => void handleDeleteFile(row.file)}
              >
                <Button type="link" size="small" danger icon={<DeleteOutlined />}>
                  删除
                </Button>
              </Popconfirm>
            </Space>
          );
        }
        return (
          <Space wrap size={0}>
            <Button
              type="link"
              size="small"
              icon={<EditOutlined />}
              onClick={() => openRename(row.folder)}
            >
              重命名
            </Button>
            <Button
              type="link"
              size="small"
              icon={<ShareAltOutlined />}
              onClick={() => setShareTarget({ folder: row.folder })}
            >
              分享
            </Button>
            <Button
              type="link"
              size="small"
              icon={<TeamOutlined />}
              onClick={() => setPermTarget({ folder: row.folder })}
            >
              权限
            </Button>
            {/* 破坏性操作必须二次确认（docs/frontend.md） */}
            <Popconfirm
              title={`删除「${row.folder.name}」？`}
              description="该目录及其子目录会移入回收站，可在回收站恢复。"
              okText="删除"
              okButtonProps={{ danger: true }}
              cancelText="取消"
              onConfirm={() => void handleDeleteFolder(row.folder)}
            >
              <Button type="link" size="small" danger icon={<DeleteOutlined />}>
                删除
              </Button>
            </Popconfirm>
          </Space>
        );
      },
    },
  ];

  return (
    <Card>
      <FolderBreadcrumb chain={chain} />

      <Space style={{ marginBottom: 12 }} wrap>
        <Button icon={<PlusOutlined />} onClick={openCreate} disabled={!currentId}>
          新建文件夹
        </Button>
        <Button
          icon={<ReloadOutlined />}
          onClick={() => {
            void foldersQuery.refetch();
            void childrenQuery.refetch();
          }}
          loading={foldersQuery.isFetching || childrenQuery.isFetching}
        >
          刷新
        </Button>

        {/* 过滤当前页：真正的服务端搜索属于后续阶段（见文件头说明） */}
        <Input.Search
          allowClear
          placeholder="在当前页中按名称过滤"
          style={{ width: 240 }}
          value={keyword}
          onChange={(event) => setKeyword(event.target.value)}
        />

        <Dropdown
          menu={{
            selectable: true,
            // key 用字段名本身：选中项要高亮当前排序字段，方向由标签里的箭头表示
            selectedKeys: [sort.field],
            items: SORT_FIELDS.map((field) => ({
              key: field,
              label: `${SORT_FIELD_LABELS[field]}${sortIndicator(sort, field)}`,
            })),
            onClick: ({ key }) => {
              setSort((current) => nextSortState(current, key as SortField));
            },
          }}
        >
          <Button icon={<SortAscendingOutlined />}>
            排序：{SORT_FIELD_LABELS[sort.field]}
            {sort.order === 'asc' ? ' 升序' : ' 降序'}
          </Button>
        </Dropdown>

        <Segmented
          value={viewMode}
          onChange={(value) => setViewMode(value as ViewMode)}
          options={[
            { value: 'list', icon: <UnorderedListOutlined />, label: '列表' },
            { value: 'grid', icon: <AppstoreOutlined />, label: '网格' },
          ]}
        />

        {keyword.trim() && (
          <Typography.Text type="secondary">
            过滤后 {rows.length} 项
          </Typography.Text>
        )}
      </Space>

      <UploadPanel folderId={currentId} disabled={!user} />

      {state === 'idle' && (
        <Empty
          image={Empty.PRESENTED_IMAGE_SIMPLE}
          description={pageStateMessage('idle')}
        />
      )}

      {showsSpinner(state) && (
        <div style={{ padding: 24, textAlign: 'center' }}>
          <Spin tip={pageStateMessage(state)}>
            <div style={{ width: 200, height: 40 }} />
          </Spin>
        </div>
      )}

      {showsErrorPlaceholder(state) && (
        <Empty
          description={
            <span>
              {pageStateMessage(state, rootQuery.error ?? pathQuery.error ?? foldersQuery.error ?? childrenQuery.error)}
              <br />
              <Typography.Text type="secondary">
                当前目录「{currentLabel || '未知'}」
              </Typography.Text>
            </span>
          }
        />
      )}

      {showsContent(state) && viewMode === 'list' && (
        <Table<BrowseRow>
          rowKey="key"
          columns={columns}
          dataSource={rows}
          pagination={false}
          locale={{
            emptyText: (
              <Empty
                image={Empty.PRESENTED_IMAGE_SIMPLE}
                description={
                  keyword.trim() ? '当前页没有匹配的名称' : '这个文件夹是空的，点击「上传文件」开始'
                }
              />
            ),
          }}
        />
      )}

      {showsContent(state) && viewMode === 'grid' && (
        <FileGrid
          rows={rows}
          onOpenFolder={(row) => navigate(`/files/${row.folder.id}`)}
          onDownload={(row) => void handleDownload(row.file)}
          onDetail={(row) => setDetailTarget(row.file)}
          onVersions={(row) => setVersionTarget(row.file)}
          onShare={(row) =>
            setShareTarget(row.kind === 'file' ? { file: row.file } : { folder: row.folder })
          }
          onDelete={handleRowDelete}
        />
      )}

      {showsContent(state) &&
        childrenQuery.data &&
        childrenQuery.data.total > childrenQuery.data.items.length && (
          <Typography.Paragraph type="secondary" style={{ marginTop: 12 }}>
            共 {childrenQuery.data.total} 个文件，当前显示前 {childrenQuery.data.items.length} 个
            （分页控件在后续阶段实现，排序与过滤也只作用于已加载的这一页）
          </Typography.Paragraph>
        )}

      {/* 文件详情抽屉：三个弹窗仍由本页持有，抽屉只是入口 */}
      <FileDetailDrawer
        file={detailTarget}
        open={detailTarget !== null}
        onClose={() => setDetailTarget(null)}
        onOpenVersions={(file) => setVersionTarget(file)}
        onOpenShare={(file) => setShareTarget({ file })}
        onOpenPermissions={(file) => setPermTarget({ file })}
      />

      {/* 版本历史：只增不减，恢复历史版本会追加新版本 */}
      <VersionHistoryModal
        file={versionTarget}
        open={versionTarget !== null}
        onClose={() => setVersionTarget(null)}
        onUpdated={() => {
          void childrenQuery.refetch();
        }}
      />

      {/* 分享与权限：创建后的明文 Token 只展示一次；权限判定始终在后端 */}
      <ShareDialog
        file={shareTarget?.file ?? null}
        folder={shareTarget?.folder ?? null}
        open={shareTarget !== null}
        onClose={() => setShareTarget(null)}
      />
      <PermissionDialog
        file={permTarget?.file ?? null}
        folder={permTarget?.folder ?? null}
        open={permTarget !== null}
        onClose={() => setPermTarget(null)}
      />

      <Modal
        open={editor !== null}
        title={editor?.kind === 'rename' ? '重命名文件夹' : '新建文件夹'}
        okText={editor?.kind === 'rename' ? '重命名' : '创建'}
        cancelText="取消"
        confirmLoading={createMutation.isPending || renameMutation.isPending}
        onOk={() => void submitEditor()}
        onCancel={() => setEditor(null)}
        destroyOnClose
      >
        <Form form={form} layout="vertical" preserve={false}>
          <Form.Item
            label="名称"
            name="name"
            rules={[
              { required: true, message: '请输入文件夹名称' },
              { max: 255, message: '名称过长' },
              {
                // 与后端 ValidateFolderName 一致；后端仍会独立校验
                validator(_rule, value: string) {
                  const name = (value ?? '').trim();
                  if (!name) return Promise.resolve();
                  if (name === '.' || name === '..') {
                    return Promise.reject(new Error('名称不能是 . 或 ..'));
                  }
                  if (/[/\\]/.test(name)) {
                    return Promise.reject(new Error('名称不能包含路径分隔符'));
                  }
                  return Promise.resolve();
                },
              },
            ]}
          >
            <Input autoFocus placeholder="例如：项目文档" onPressEnter={() => void submitEditor()} />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  );
}
