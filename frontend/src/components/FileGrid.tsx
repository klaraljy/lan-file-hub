/**
 * 文件网格视图（docs/frontend.md §2.2「文件列表或网格切换」）。
 *
 * 与列表视图共用同一份数据与同一批操作，只是换了排版：
 * 文件夹在前，卡片上给出名称、大小/类型与常用操作。
 * 只负责渲染与转发点击，不自己发请求。
 */
import { Button, Card, Col, Empty, Popconfirm, Row, Space, Tooltip, Typography } from 'antd';
import {
  DeleteOutlined,
  DownloadOutlined,
  EyeOutlined,
  FileOutlined,
  FolderOutlined,
  HistoryOutlined,
  InfoCircleOutlined,
  SendOutlined,
  ShareAltOutlined,
} from '@ant-design/icons';

import { formatBytes, typeLabel, type BrowseRow } from '../features/files/listView';
import { TOUCH_HEIGHT, useIsMobile } from '../hooks/useIsMobile';

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
  onDelete,
}: Props) {
  const isMobile = useIsMobile();
  // 手机上图标按钮要够大才点得准（AGENTS.md §14.1）
  const iconButtonProps = isMobile
    ? { size: 'middle' as const, style: { minWidth: TOUCH_HEIGHT, height: TOUCH_HEIGHT } }
    : { size: 'small' as const };

  if (rows.length === 0) {
    return (
      <Empty
        image={Empty.PRESENTED_IMAGE_SIMPLE}
        description="这个文件夹是空的，点击「上传文件」开始"
      />
    );
  }

  // 列数随宽度自适应：手机 1 列（卡片太窄时按钮会挤在一起）、窄屏 2 列、宽屏更多列
  return (
    <Row gutter={[12, 12]}>
      {rows.map((row) => (
        <Col key={row.key} xs={24} sm={12} md={8} lg={6} xl={4}>
          <Card
            size="small"
            hoverable
            styles={{ body: { padding: 12 } }}
            onClick={() => (row.kind === 'folder' ? onOpenFolder(row) : onDetail(row))}
          >
            <Space direction="vertical" size={4} style={{ width: '100%' }}>
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
                {row.kind === 'folder' ? '文件夹' : `${formatBytes(row.size_bytes)} · ${typeLabel(row.file)}`}
              </Typography.Text>

              {/* 按人发送：主按钮，放在最显眼的位置（AGENTS.md §14.3） */}
              <Button
                type="primary"
                size={isMobile ? 'large' : 'small'}
                block
                icon={<SendOutlined />}
                onClick={(event) => {
                  event.stopPropagation();
                  onSend(row);
                }}
              >
                发送给…
              </Button>

              {/* 点击卡片本身是「打开/详情」，图标按钮承担具体操作 */}
              <Space size={isMobile ? 4 : 0} style={{ width: '100%', justifyContent: 'center' }}>
                {row.kind === 'file' ? (
                  <>
                    <Tooltip title="下载">
                      <Button
                        type="text"
                        {...iconButtonProps}
                        icon={<DownloadOutlined />}
                        onClick={(event) => {
                          event.stopPropagation();
                          onDownload(row);
                        }}
                      />
                    </Tooltip>
                    <Tooltip title="预览">
                      <Button
                        type="text"
                        {...iconButtonProps}
                        icon={<EyeOutlined />}
                        onClick={(event) => {
                          event.stopPropagation();
                          onPreview(row);
                        }}
                      />
                    </Tooltip>
                    <Tooltip title="版本历史">
                      <Button
                        type="text"
                        {...iconButtonProps}
                        icon={<HistoryOutlined />}
                        onClick={(event) => {
                          event.stopPropagation();
                          onVersions(row);
                        }}
                      />
                    </Tooltip>
                    <Tooltip title="详情">
                      <Button
                        type="text"
                        {...iconButtonProps}
                        icon={<InfoCircleOutlined />}
                        onClick={(event) => {
                          event.stopPropagation();
                          onDetail(row);
                        }}
                      />
                    </Tooltip>
                  </>
                ) : null}
                <Tooltip title="分享">
                  <Button
                    type="text"
                    {...iconButtonProps}
                    icon={<ShareAltOutlined />}
                    onClick={(event) => {
                      event.stopPropagation();
                      onShare(row);
                    }}
                  />
                </Tooltip>
                {/* 破坏性操作必须二次确认（docs/frontend.md） */}
                <Popconfirm
                  title={`删除「${row.name}」？`}
                  description={
                    row.kind === 'folder'
                      ? '该目录及其子目录会移入回收站，可在回收站恢复。'
                      : '文件会移入回收站，可以恢复。'
                  }
                  okText="删除"
                  okButtonProps={{ danger: true }}
                  cancelText="取消"
                  onConfirm={() => onDelete(row)}
                >
                  <Button
                    type="text"
                    {...iconButtonProps}
                    danger
                    icon={<DeleteOutlined />}
                    onClick={(event) => event.stopPropagation()}
                  />
                </Popconfirm>
              </Space>
            </Space>
          </Card>
        </Col>
      ))}
    </Row>
  );
}
