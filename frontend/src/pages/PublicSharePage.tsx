/**
 * 公开分享落地页（路由 /s/:token，**不需要登录**）。
 *
 * 这是唯一一个非登录页面：拿到 token 就能看分享详情并下载（如果允许）。
 *   - 文件：展示文件名、大小、类型、权限、剩余下载次数、到期时间，可直接下载；
 *   - 文件夹：展示基本信息，并明确告知「暂不支持打包下载」；
 *   - 链接失效（不存在 / 已撤销 / 已过期 / 次数用完）：展示可理解的原因。
 *
 * 下载为什么用 window.location.href 而不是 axios：
 * 公开接口不需要 Authorization 头，交给浏览器直接导航能得到原生的下载体验
 * （也不用把整个文件读进内存，见 filesApi.download 的已知限制说明）。
 */
import { Button, Card, Empty, Result, Space, Spin, Tag, Typography } from 'antd';
import { DownloadOutlined, FileOutlined, FolderOpenOutlined } from '@ant-design/icons';
import { useQuery } from '@tanstack/react-query';
import { useParams } from 'react-router-dom';
import type { ReactNode } from 'react';

import { formatBytes } from '../features/files/listView';
import {
  downloadQuotaLabel,
  expiryLabel,
  mapPublicShareState,
  publicDownloadUrl,
  publicShareErrorMessage,
} from '../features/sharing/publicShareState';
import { sharingApi } from '../services/sharingApi';
import type { SharePermission } from '../types/share';

const PERMISSION_TEXT: Record<SharePermission, string> = {
  READ: '只读（可查看与下载）',
  WRITE: '可写',
};

export function PublicSharePage() {
  const params = useParams<{ token: string }>();
  const token = params.token ?? '';

  const query = useQuery({
    queryKey: ['public-share', token],
    queryFn: () => sharingApi.describePublic(token),
    enabled: Boolean(token),
    // 分享内容可能随时被撤销或过期，不做长时间缓存
    staleTime: 0,
    retry: false,
  });

  const state = mapPublicShareState({
    isLoading: query.isLoading,
    isError: query.isError,
    error: query.error,
    data: query.data ?? null,
  });

  if (!token) {
    return (
      <CenteredCard>
        <Result status="warning" title="分享链接不完整" subTitle="请向分享者索取完整链接。" />
      </CenteredCard>
    );
  }

  if (state === 'loading') {
    return (
      <CenteredCard>
        <Spin tip="正在打开分享…">
          <div style={{ width: 200, height: 40 }} />
        </Spin>
      </CenteredCard>
    );
  }

  if (state === 'error') {
    return (
      <CenteredCard>
        <Result
          status="warning"
          title="无法打开这个分享"
          subTitle={publicShareErrorMessage(query.error)}
          extra={
            <Button onClick={() => void query.refetch()} loading={query.isFetching}>
              重试
            </Button>
          }
        />
      </CenteredCard>
    );
  }

  const view = query.data;
  if (!view) {
    return (
      <CenteredCard>
        <Empty description="分享内容为空" />
      </CenteredCard>
    );
  }

  const isFolder = view.resource_type === 'folder';

  return (
    <CenteredCard title="有人分享了一个内容给你">
      <Space direction="vertical" size="middle" style={{ width: '100%' }}>
        <Space size={8} wrap>
          {isFolder ? (
            <FolderOpenOutlined style={{ fontSize: 20, color: '#faad14' }} />
          ) : (
            <FileOutlined style={{ fontSize: 20, color: '#8c8c8c' }} />
          )}
          <Typography.Title level={4} style={{ margin: 0 }}>
            {view.name}
          </Typography.Title>
          <Tag>{isFolder ? '文件夹' : '文件'}</Tag>
          <Tag color="blue">{PERMISSION_TEXT[view.permission] ?? view.permission}</Tag>
        </Space>

        <Space direction="vertical" size={2}>
          {!isFolder && (
            <Typography.Text type="secondary">
              大小：{formatBytes(view.size_bytes)}
            </Typography.Text>
          )}
          {!isFolder && view.mime_type && (
            <Typography.Text type="secondary">类型：{view.mime_type}</Typography.Text>
          )}
          <Typography.Text type="secondary">{downloadQuotaLabel(view)}</Typography.Text>
          <Typography.Text type="secondary">{expiryLabel(view)}</Typography.Text>
        </Space>

        {isFolder && (
          <Typography.Text type="warning">文件夹分享暂不支持打包下载，请联系分享者获取具体文件。</Typography.Text>
        )}

        {!isFolder && view.can_download && (
          <Button
            type="primary"
            icon={<DownloadOutlined />}
            // 公开接口不带 Authorization，浏览器直接导航即可完成下载
            onClick={() => {
              window.location.href = publicDownloadUrl(token);
            }}
          >
            下载文件
          </Button>
        )}

        {!isFolder && !view.can_download && (
          <Typography.Text type="danger">
            {view.download_blocked_reason || '这个分享当前不可下载'}
          </Typography.Text>
        )}

        <Typography.Text type="secondary" style={{ fontSize: 12 }}>
          分享由 lan-file-hub 提供。链接可能被分享者随时撤销或设置到期时间。
        </Typography.Text>
      </Space>
    </CenteredCard>
  );
}

/** 公开页是独立的落地页，不需要应用外壳，居中展示即可 */
function CenteredCard({ children, title }: { children: ReactNode; title?: string }) {
  return (
    <div
      style={{
        minHeight: '100vh',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        padding: 24,
        background: '#f5f5f5',
      }}
    >
      <Card title={title} style={{ width: '100%', maxWidth: 560 }}>
        {children}
      </Card>
    </div>
  );
}
