/**
 * 文件网格视图（docs/frontend.md §2.2「文件列表或网格切换」）。
 *
 * 与列表视图共用同一份数据与同一批操作，只是换了排版：
 * 文件夹在前，卡片上给出名称、大小/类型与常用操作。
 *
 * 2026-10-03 用户投诉按钮太乱之后：卡片上**只有三个控件** ——
 * 主按钮（文件=下载、文件夹=打开）、次要按钮「发送给…」、以及「更多」下拉；
 * 原先那排 5~6 个图标按钮全部进下拉（动作定义在 `features/files/rowActions.ts`）。
 * 只负责渲染与转发点击，不自己发请求。
 */
import { Button, Card, Col, Dropdown, Empty, Row, Space, Tooltip, Typography } from 'antd';
import type { MenuProps } from 'antd';
import {
  DeleteOutlined,
  DownloadOutlined,
  EditOutlined,
  EyeOutlined,
  FileOutlined,
  FolderOpenOutlined,
  FolderOutlined,
  HistoryOutlined,
  InfoCircleOutlined,
  MoreOutlined,
  SendOutlined,
  ShareAltOutlined,
  TeamOutlined,
} from '@ant-design/icons';

import { formatBytes, typeLabel, type BrowseRow } from '../features/files/listView';
import { rowActions, type RowActionKey } from '../features/files/rowActions';
import { canPreview } from '../services/filePreview';
import { TOUCH_HEIGHT, useIsMobile } from '../hooks/useIsMobile';

const ACTION_ICONS: Record<RowActionKey, React.ReactNode> = {
  open: <FolderOpenOutlined />,
  download: <DownloadOutlined />,
  downloadAll: <DownloadOutlined />,
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
  onDownload: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  onDetail: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  /** 预览（2026-10-03）：图片/PDF/文本在网页里直接看 */
  onPreview: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  onVersions: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  onShare: (row: BrowseRow) => void;
  /** 「发送给…」：按人发送（AGENTS.md §14.3） */
  onSend: (row: BrowseRow) => void;
  /** 文件夹「下载」：下载里面的全部文件（2026-10-03 用户要求，不打包） */
  onDownloadFolder: (row: Extract<BrowseRow, { kind: 'folder' }>) => void;
  onDelete: (row: BrowseRow) => void;
}

export function FileGrid({
  rows,
  onOpenFolder,
  onDownload,
  onDetail,
  onPreview,
  onVersions,
  onShare,
  onSend,
  onDownloadFolder,
  onDelete,
}: Props) {
  const isMobile = useIsMobile();
  const buttonHeight = isMobile ? { height: TOUCH_HEIGHT } : undefined;

  if (rows.length === 0) {
    return (
      <Empty
        image={Empty.PRESENTED_IMAGE_SIMPLE}
        description="这个文件夹是空的，点击「上传文件」开始"
      />
    );
  }

  function dispatch(row: BrowseRow, key: RowActionKey) {
    if (row.kind === 'file') {
      if (key === 'download') onDownload(row);
      if (key === 'preview') onPreview(row);
      if (key === 'detail') onDetail(row);
      if (key === 'versions') onVersions(row);
    } else {
      if (key === 'open') onOpenFolder(row);
      if (key === 'downloadAll') onDownloadFolder(row);
      // 网格里没有「重命名」弹窗入口，交给详情/列表视图
    }
    if (key === 'send') onSend(row);
    if (key === 'share') onShare(row);
    if (key === 'delete') onDelete(row);
  }

  // 列数随宽度自适应：手机 1 列（卡片太窄时按钮会挤在一起）、窄屏 2 列、宽屏更多列
  return (
    <Row gutter={[12, 12]}>
      {rows.map((row) => {
        const set = rowActions(row.kind, {
          canPreview: row.kind === 'file' && canPreview(row.file.mime_type, row.name),
        });
        const primary = set.inline[0]!;
        const menu: MenuProps['items'] = set.menu.map((action) => ({
          key: action.key,
          icon: ACTION_ICONS[action.key],
          label: action.label,
          danger: action.danger,
        }));

        return (
          <Col key={row.key} xs={24} sm={12} md={8} lg={6} xl={4}>
            <Card
              size="small"
              hoverable
              styles={{ body: { padding: 12 } }}
              onClick={() => (row.kind === 'folder' ? onOpenFolder(row) : onDetail(row))}
            >
              <Space direction="vertical" size={6} style={{ width: '100%' }}>
                <div style={{ fontSize: 28, textAlign: 'center' }}>
                  {row.kind === 'folder' ? (
                    <FolderOutlined style={{ color: '#faad14' }} />
                  ) : (
                    <FileOutlined style={{ color: '#8c8c8c' }} />
                  )}
                </div>
                <Tooltip title={row.name}>
                  <Typography.Text ellipsis style={{ display: 'block', textAlign: 'center' }}>
                    {row.name}
                  </Typography.Text>
                </Tooltip>
                <Typography.Text
                  type="secondary"
                  style={{ display: 'block', textAlign: 'center', fontSize: 12 }}
                >
                  {row.kind === 'folder'
                    ? '文件夹'
                    : `${formatBytes(row.size_bytes)} · ${typeLabel(row.file)}`}
                </Typography.Text>

                {/* 主按钮：文件=下载、文件夹=打开（用户 2026-10-03：手机上找不到下载按钮） */}
                <Button
                  type="primary"
                  // 桌面网格用描边款：一屏几十张卡片，实心蓝会糊成一片（用户投诉过颜色）
                  ghost={!isMobile}
                  size={isMobile ? 'middle' : 'small'}
                  block
                  icon={ACTION_ICONS[primary.key]}
                  style={buttonHeight}
                  onClick={(event) => {
                    event.stopPropagation();
                    dispatch(row, primary.key);
                  }}
                >
                  {primary.label}
                </Button>

                <Space size={8} style={{ width: '100%', justifyContent: 'center' }}>
                  {/* 「发送给…」留在卡片上（AGENTS §14.3），但不再抢主按钮 */}
                  <Button
                    size={isMobile ? 'middle' : 'small'}
                    icon={<SendOutlined />}
                    style={buttonHeight}
                    onClick={(event) => {
                      event.stopPropagation();
                      onSend(row);
                    }}
                  >
                    发送给…
                  </Button>
                  <Dropdown
                    trigger={['click']}
                    menu={{
                      items: menu,
                      onClick: ({ key, domEvent }) => {
                        domEvent.stopPropagation();
                        dispatch(row, key as RowActionKey);
                      },
                    }}
                  >
                    <Button
                      size={isMobile ? 'middle' : 'small'}
                      icon={<MoreOutlined />}
                      aria-label={`更多操作：${row.name}`}
                      style={buttonHeight}
                      onClick={(event) => event.stopPropagation()}
                    />
                  </Dropdown>
                </Space>
              </Space>
            </Card>
          </Col>
        );
      })}
    </Row>
  );
}
