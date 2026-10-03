/**
 * 手机上用的文件卡片列表（AGENTS.md §14.1：手机上"憋屈"是产品缺陷）。
 *
 * 为什么不用宽表格：表格有 5 列 + 一行 7 个操作按钮，手机宽度下必然横向溢出、按钮点不准。
 * 这里一条文件一张卡片：**名称 + 大小/类型/时间 + 一行操作**，
 * 其中「发送给…」保持**显眼的主按钮**（AGENTS.md §14.3 用户明确要求），
 * 其余操作（下载/详情/版本/分享/权限/删除）收进「更多」下拉。
 *
 * 只负责渲染与转发点击，不自己发请求（与 FileGrid 的约定一致）。
 */
import { App as AntApp, Button, Card, Dropdown, Empty, Typography } from 'antd';
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
import { TOUCH_HEIGHT } from '../hooks/useIsMobile';

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
    const items: MenuProps['items'] =
      row.kind === 'file'
        ? [
            { key: 'preview', icon: <EyeOutlined />, label: '预览' },
            { key: 'download', icon: <DownloadOutlined />, label: '下载' },
            { key: 'detail', icon: <InfoCircleOutlined />, label: '详情' },
            { key: 'versions', icon: <HistoryOutlined />, label: '版本历史' },
          ]
        : [
            { key: 'open', icon: <FolderOpenOutlined />, label: '打开' },
            { key: 'rename', icon: <EditOutlined />, label: '重命名' },
          ];
    return [
      ...items,
      { key: 'share', icon: <ShareAltOutlined />, label: '分享' },
      { key: 'permission', icon: <TeamOutlined />, label: '权限' },
      { type: 'divider' },
      { key: 'delete', icon: <DeleteOutlined />, label: '删除', danger: true },
    ];
  }

  function handleMore(row: BrowseRow, key: string) {
    if (row.kind === 'file') {
      if (key === 'preview') onPreview(row);
      if (key === 'download') onDownload(row);
      if (key === 'detail') onDetail(row);
      if (key === 'versions') onVersions(row);
    } else {
      if (key === 'open') onOpenFolder(row);
      if (key === 'rename') onRename(row);
    }
    if (key === 'share') onShare(row);
    if (key === 'permission') onPermission(row);
    if (key === 'delete') confirmDelete(row);
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
            style={{ display: 'flex', gap: 8, marginTop: 10, alignItems: 'center' }}
            onClick={(event) => event.stopPropagation()}
          >
            {/* 主操作：按人发送（AGENTS.md §14.3）。手机上必须是显眼的大按钮 */}
            <Button
              type="primary"
              icon={<SendOutlined />}
              style={{ flex: 1, height: TOUCH_HEIGHT }}
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
          </div>
        </Card>
      ))}
    </div>
  );
}
