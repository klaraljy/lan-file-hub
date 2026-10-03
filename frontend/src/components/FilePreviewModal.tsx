/**
 * 文件预览弹窗（2026-10-03，用户要求"图片缩略图/预览"）。
 *
 * 三种能预览的类型：图片（`<img>`，地址带一次性票据）、PDF（`<iframe>`，同上）、
 * 文本（带 Token 读回来，超过上限只显示开头）。其它类型直接告诉用户"下载查看"，
 * **不要**假装能打开（AGENTS §14.1：不会用 = 缺陷，界面不许骗人）。
 */
import { useEffect, useState } from 'react';
import { Alert, Button, Space, Spin, Typography } from 'antd';
import { DownloadOutlined } from '@ant-design/icons';

import { ResponsiveSheet } from './ResponsiveSheet';
import {
  fetchPreviewUrl,
  fetchTextPreview,
  previewKind,
  type PreviewKind,
} from '../services/filePreview';

interface Props {
  open: boolean;
  fileId: string;
  fileName: string;
  mimeType?: string;
  /** 下载：预览不了或用户看完想存下来时用（走浏览器原生下载） */
  onDownload: () => void;
  onClose: () => void;
}

export function FilePreviewModal({ open, fileId, fileName, mimeType, onDownload, onClose }: Props) {
  const kind: PreviewKind = previewKind(mimeType, fileName);
  const [url, setUrl] = useState('');
  const [text, setText] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    if (!open) {
      setUrl('');
      setText('');
      setError('');
      return;
    }
    let cancelled = false;
    setLoading(true);
    setError('');
    const task =
      kind === 'text'
        ? fetchTextPreview(fileId).then((content) => {
            if (!cancelled) setText(content);
          })
        : kind === 'image' || kind === 'pdf'
          ? fetchPreviewUrl(fileId).then((address) => {
              if (!cancelled) setUrl(address);
            })
          : Promise.resolve();
    task
      .catch(() => {
        if (!cancelled) setError('打不开这个文件，请下载后再看');
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [open, fileId, kind]);

  function renderBody() {
    if (loading) {
      return (
        <div style={{ textAlign: 'center', padding: 32 }}>
          <Spin />
        </div>
      );
    }
    if (error) {
      return <Alert type="warning" showIcon message={error} />;
    }
    if (kind === 'image' && url) {
      return (
        <img
          src={url}
          alt={fileName}
          style={{ maxWidth: '100%', maxHeight: '70vh', display: 'block', margin: '0 auto' }}
        />
      );
    }
    if (kind === 'pdf' && url) {
      return <iframe title={fileName} src={url} style={{ width: '100%', height: '70vh', border: 0 }} />;
    }
    if (kind === 'text') {
      return (
        <pre
          style={{
            maxHeight: '70vh',
            overflow: 'auto',
            background: '#fafafa',
            padding: 12,
            borderRadius: 6,
            fontSize: 12,
            whiteSpace: 'pre-wrap',
            wordBreak: 'break-all',
          }}
        >
          {text}
        </pre>
      );
    }
    return (
      <Typography.Paragraph type="secondary">
        这种文件不能在网页里直接打开（只有图片、PDF 和文本可以），请点下面的「下载」保存到设备上看。
      </Typography.Paragraph>
    );
  }

  return (
    <ResponsiveSheet open={open} onClose={onClose} title={fileName} desktopWidth={860}>
      <Space direction="vertical" size={12} style={{ width: '100%' }}>
        {renderBody()}
        <Button icon={<DownloadOutlined />} onClick={onDownload} block>
          下载
        </Button>
      </Space>
    </ResponsiveSheet>
  );
}
