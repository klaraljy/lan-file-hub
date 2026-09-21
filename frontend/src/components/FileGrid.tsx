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
  FileOutlined,
  FolderOutlined,
  HistoryOutlined,
  InfoCircleOutlined,
  SendOutlined,
  ShareAltOutlined,
} from '@ant-design/icons';

import { formatBytes, typeLabel, type BrowseRow } from '../features/files/listView';

interface Props {
  rows: BrowseRow[];
  onOpenFolder: (row: Extract<BrowseRow, { kind: 'folder' }>) => void;
  onDownload: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
  onDetail: (row: Extract<BrowseRow, { kind: 'file' }>) => void;
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
  onVersions,
  onShare,
  onSend,
  onDelete,
}: Props) {
  if (rows.length === 0) {
    return (
      <Empty
        image={Empty.PRESENTED_IMAGE_SIMPLE}
        description="这个文件夹是空的，点击「上传文件」开始"
      />
    );
  }

  return (
    <Row gutter={[12, 12]}>
      {rows.map((row) => (
        <Col key={row.key} xs={12} sm={8} md={6} lg={6} xl={4}>
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
                size="small"
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
              <Space size={0} style={{ width: '100%', justifyContent: 'center' }}>
                {row.kind === 'file' ? (
                  <>
                    <Tooltip title="下载">
                      <Button
                        type="text"
                        size="small"
                        icon={<DownloadOutlined />}
                        onClick={(event) => {
                          event.stopPropagation();
                          onDownload(row);
                        }}
                      />
                    </Tooltip>
                    <Tooltip title="版本历史">
                      <Button
                        type="text"
                        size="small"
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
                        size="small"
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
                    size="small"
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
                    size="small"
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
