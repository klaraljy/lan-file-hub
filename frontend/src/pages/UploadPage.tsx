/**
 * 上传页（2026-10-03 用户要求：「把上传文件和我的文件分开来换成两个目录做，这样起码好看一点」）。
 *
 * 左侧导航第二个入口「上传文件」。跟「我的文件」的分工：
 *   - 这里只干上传：选目标文件夹 + 拖/选文件 + 看任务进度与失败重试；
 *   - 「我的文件」只干浏览与管理（列表/网格/目录树/下载/删除/分享）。
 *
 * 目标文件夹来自 `?folder=<id>`（从「我的文件」点「上传到这里」带过来），没带就是根目录。
 * 左边复用「我的文件」那棵目录树来选目标 —— 一套组件两处用，不另造一个选择器。
 */
import { useMemo, useState } from 'react';
import { App as AntApp, Alert, Button, Card, Space, Typography } from 'antd';
import { FolderOpenOutlined, FolderOutlined } from '@ant-design/icons';
import { useNavigate, useSearchParams } from 'react-router-dom';

import { FolderTree } from '../components/FolderTree';
import { UploadPanel } from '../components/UploadPanel';
import { FolderBreadcrumb } from '../components/FolderBreadcrumb';
import { ResponsiveSheet } from '../components/ResponsiveSheet';
import { useFolderPath, useRootFolder } from '../features/folders/useFolders';
import { resolveUploadTarget } from '../features/uploads/uploadTarget';
import { useIsMobile } from '../hooks/useIsMobile';
import type { Folder } from '../types/folder';

export function UploadPage() {
  const [searchParams, setSearchParams] = useSearchParams();
  const navigate = useNavigate();
  const isMobile = useIsMobile();
  const { message } = AntApp.useApp();
  const [mobileTreeOpen, setMobileTreeOpen] = useState(false);

  const rootQuery = useRootFolder();
  const targetId = resolveUploadTarget(searchParams.get('folder'), rootQuery.data?.id);
  const pathQuery = useFolderPath(targetId);
  const chain = useMemo<Folder[]>(() => pathQuery.data ?? [], [pathQuery.data]);
  const targetLabel = chain.length > 0 ? chain[chain.length - 1] : undefined;

  const selectFolder = (folderId: string) => {
    setSearchParams({ folder: folderId }, { replace: true });
  };

  const tree = (
    <FolderTree
      root={rootQuery.data}
      currentId={targetId}
      chain={chain}
      onSelect={selectFolder}
    />
  );

  return (
    <Card
      title="上传文件"
      extra={
        <Button
          icon={<FolderOpenOutlined />}
          onClick={() => navigate(targetId ? `/files/${targetId}` : '/files')}
        >
          去「我的文件」看
        </Button>
      }
    >
      <Space direction="vertical" size="middle" style={{ width: '100%' }}>
        {/* 传到哪儿必须一眼看到：传错文件夹是用户最容易困惑的事 */}
        <Alert
          type="info"
          showIcon
          icon={<FolderOutlined />}
          message={
            <span>
              文件会传到：<b>{targetLabel ? (targetLabel.is_root ? '我的文件（根目录）' : targetLabel.name) : '读取中…'}</b>
              {chain.length > 1 ? (
                <Typography.Text type="secondary" style={{ marginLeft: 8 }}>
                  路径：{chain.map((folder) => (folder.is_root ? '我的文件' : folder.name)).join(' / ')}
                </Typography.Text>
              ) : null}
            </span>
          }
          description="想换个文件夹：在左边（手机上点下面的「换个文件夹」）点一下目标目录即可。"
        />

        <div style={{ display: 'flex', gap: 16, alignItems: 'flex-start', flexWrap: 'wrap' }}>
          {/* 目标文件夹选择：桌面在左边一栏，手机上收进可展开区域，避免占满首屏 */}
          {!isMobile && (
            <Card size="small" title="传到哪个文件夹" style={{ width: 264, flex: '0 0 264px' }} styles={{ body: { padding: 6 } }}>
              {tree}
            </Card>
          )}

          <div style={{ flex: 1, minWidth: isMobile ? '100%' : 320 }}>
            {isMobile && (
              <Space direction="vertical" size={8} style={{ width: '100%', marginBottom: 8 }}>
                <FolderBreadcrumb chain={chain} />
                <Button
                  block
                  icon={<FolderOutlined />}
                  onClick={() => {
                    // 手机上目录树放在底部面板里（见下方 ResponsiveSheet）
                    setMobileTreeOpen(true);
                  }}
                >
                  换个文件夹
                </Button>
              </Space>
            )}
            <UploadPanel folderId={targetId} disabled={!targetId} />
            <Typography.Paragraph type="secondary" style={{ marginTop: 12, marginBottom: 0 }}>
              传完的文件在「我的文件」里就能看到（同一个文件夹下）。上传中断或失败的任务会留在上面的任务列表里，
              点「重试」继续，刷新页面也不会丢。
            </Typography.Paragraph>
          </div>
        </div>
      </Space>

      {isMobile && (
        <ResponsiveSheet
          open={mobileTreeOpen}
          title="传到哪个文件夹"
          mobileHeight="70%"
          onClose={() => setMobileTreeOpen(false)}
        >
          {tree}
          <Button
            block
            size="large"
            type="primary"
            style={{ marginTop: 12 }}
            onClick={() => {
              setMobileTreeOpen(false);
              message.success('已选定目标文件夹');
            }}
          >
            就用这个文件夹
          </Button>
        </ResponsiveSheet>
      )}
    </Card>
  );
}
