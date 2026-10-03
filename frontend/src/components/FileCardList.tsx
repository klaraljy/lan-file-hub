/**
 * 手机上用的文件卡片列表（AGENTS.md §14.1：手机上"憋屈"是产品缺陷）。
 *
 * 为什么不用宽表格：表格有一行七个操作按钮，手机宽度下必然横向溢出、按钮点不准。
 * 这里一条文件一张卡片：**名称 + 大小/类型/时间 + 一行操作**。
 *
 * 2026-10-03 用户投诉"手机端一大个发送给按钮，我的下载按钮呢"之后的排版：
 *   **主按钮 = 这一行最常用的动作**（文件=下载、文件夹=打开），占满一行、44px 高；
 *   下面是「发送给…」（次要按钮，仍然在行上，不违反 AGENTS §14.3）和「更多」。
 *
 * 只负责渲染与转发点击，不自己发请求（与 FileGrid 的约定一致）。
 */
import { App as AntApp, Button, Card, Dropdown, Empty, Space, Typography } from 'antd';
import type { MenuProps } from 'antd';
import {
  DeleteOutlined,
  DownloadOutlined,
  EditOutlined,
  EyeOutlined,
  FolderOpenOutlined,
  FolderOutlined,
  HistoryOutlined,
  InfoCircleOutlined,
  MoreOutlined,
  RightOutlined,
  SendOutlined,
  ShareAltOutlined,
  TeamOutlined,
} from '@ant-design/icons';

import { FileThumbnail } from './FileThumbnail';
import { formatBytes, typeLabel, type BrowseRow } from '../features/files/listView';
import { rowActions, type RowActionKey } from '../features/files/rowActions';
import { canPreview } from '../services/filePreview';
import { TOUCH_HEIGHT } from '../hooks/useIsMobile';

const ACTION_ICONS: Record<RowActionKey, React.ReactNode> = {
  open: <FolderOpenOutlined />,
  download: <DownloadOutlined />,
  preview: <EyeOutlined />,
  detail: <InfoCircleOutlined />,
  versions: <HistoryOutlined />,
  send: <SendOutlined />,
  share: <ShareAltOutlined />,
  permission: <TeamOutlined />,
  rename: <EditOutlined />,
  delete: <DeleteOutlined />,
};

interface Props {
  rows: BrowseRow[];
  onOpenFolder: (row: Extract<BrowseRow, { kind: 'folder' }>) => void;
  onRename: (row: Extract<BrowseRow, { kind: 'folder' }>) => void;
  onDownload: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  onDetail: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  /** 预览（2026-10-03）：图片/PDF/文本在网页里直接看 */
  onPreview: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  onVersions: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  onShare: (row: BrowseRow) => void;
  /** 权限管理（选人授权） */
  onPermission: (row: BrowseRow) => void;
  /** 「发送给…」：按人发送（AGENTS.md §14.3） */
  onSend: (row: BrowseRow) => void;
  onDelete: (row: BrowseRow) => void;
}

export function FileCardList({
  rows,
  onOpenFolder,
  onRename,
  onDownload,
  onDetail,
  onPreview,
  onVersions,
  onShare,
  onPermission,
  onSend,
  onDelete,
}: Props) {
  const { modal } = AntApp.useApp();

  if (rows.length === 0) {
    return (
      <Empty
        image={Empty.PRESENTED_IMAGE_SIMPLE}
        description="这个文件夹是空的，点右下角「上传」开始"
      />
    );
  }

  /** 删除是破坏性操作，必须二次确认（docs/frontend.md）——下拉里用 Modal 确认 */
  function confirmDelete(row: BrowseRow) {
    modal.confirm({
      title: `删除「${row.name}」？`,
      content:
        row.kind === 'folder'
          ? '该目录及其子目录会移入回收站，可在回收站恢复。'
          : '文件会移入回收站，可以恢复。',
      okText: '删除',
      okButtonProps: { danger: true },
      cancelText: '取消',
      onOk: () => onDelete(row),
    });
  }

  function moreItems(row: BrowseRow): MenuProps['items'] {
    const set = rowActions(row.kind, {
      canPreview: row.kind === 'file' && canPreview(row.file.mime_type, row.name),
    });
    return set.menu.map((action) => ({
      key: action.key,
      icon: ACTION_ICONS[action.key],
      label: action.label,
      danger: action.danger,
    }));
  }

  /** 行上的按钮：主按钮（下载/打开）+「发送给…」的点击都从这里走 */
  function dispatch(row: BrowseRow, key: RowActionKey) {
    if (key === 'delete') {
      confirmDelete(row);
      return;
    }
    if (row.kind === 'file') {
      if (key === 'download') onDownload(row);
      if (key === 'preview') onPreview(row);
      if (key === 'detail') onDetail(row);
      if (key === 'versions') onVersions(row);
    } else {
      if (key === 'open') onOpenFolder(row);
      if (key === 'rename') onRename(row);
    }
    if (key === 'send') onSend(row);
    if (key === 'share') onShare(row);
    if (key === 'permission') onPermission(row);
  }

  function handleMore(row: BrowseRow, key: string) {
    dispatch(row, key as RowActionKey);
  }

  return (
    // paddingBottom：给底部的悬浮上传条留位置，最后一张卡不会被挡住
    <div style={{ display: 'flex', flexDirection: 'column', gap: 10, paddingBottom: 88 }}>
      {rows.map((row) => (
        <Card
          key={row.key}
          size="small"
          hoverable
          styles={{ body: { padding: 12 } }}
          // 整张卡可点：文件夹进去、文件看详情（手机上比点小图标容易得多）
          onClick={() => (row.kind === 'folder' ? onOpenFolder(row) : onDetail(row))}
        >
          <div style={{ display: 'flex', gap: 10, alignItems: 'flex-start' }}>
            {/* 图片给缩略图（2026-10-03），其余类型保持图标：列表里几十个文件不能都去换票据 */}
            {row.kind === 'file' ? (
              <FileThumbnail
                fileId={row.file.id}
                fileName={row.name}
                mimeType={row.file.mime_type}
                size={40}
              />
            ) : (
              <span style={{ fontSize: 24, lineHeight: '30px' }}>
                <FolderOutlined style={{ color: '#faad14' }} />
              </span>
            )}
            <div style={{ flex: 1, minWidth: 0 }}>
              <Typography.Text
                strong
                ellipsis={{ tooltip: row.name }}
                style={{ display: 'block', fontSize: 16 }}
              >
                {row.name}
              </Typography.Text>
              <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                {row.kind === 'folder'
                  ? '文件夹'
                  : `${formatBytes(row.size_bytes)} · ${typeLabel(row.file)}`}
                {' · '}
                {new Date(row.updated_at).toLocaleString()}
              </Typography.Text>
            </div>
            <RightOutlined style={{ color: '#bfbfbf', fontSize: 12, marginTop: 4 }} />
          </div>

          {/* 操作区：点按钮不应该同时触发"打开卡片"，所以这里阻止冒泡 */}
          <div
            style={{ display: 'flex', flexDirection: 'column', gap: 8, marginTop: 10 }}
            onClick={(event) => event.stopPropagation()}
          >
            {/* 主按钮：这一行最常用的动作（文件=下载、文件夹=打开），占满一行最好点 */}
            <Button
              type="primary"
              block
              icon={row.kind === 'file' ? <DownloadOutlined /> : <FolderOpenOutlined />}
              style={{ height: TOUCH_HEIGHT }}
              onClick={() => dispatch(row, row.kind === 'file' ? 'download' : 'open')}
            >
              {row.kind === 'file' ? '下载' : '打开'}
            </Button>
            <Space size={8} style={{ width: '100%', justifyContent: 'space-between' }}>
              {/* 「发送给…」仍在行上（AGENTS §14.3 不许藏进"更多"），但不再抢占主按钮 */}
              <Button
                icon={<SendOutlined />}
                style={{ height: TOUCH_HEIGHT }}
                onClick={() => onSend(row)}
              >
                发送给…
              </Button>
              <Dropdown
                trigger={['click']}
                menu={{ items: moreItems(row), onClick: ({ key }) => handleMore(row, key) }}
              >
                <Button
                  aria-label={`更多操作：${row.name}`}
                  icon={<MoreOutlined />}
                  style={{ height: TOUCH_HEIGHT, minWidth: TOUCH_HEIGHT }}
                />
              </Dropdown>
            </Space>
          </div>
        </Card>
      ))}
    </div>
  );
}
