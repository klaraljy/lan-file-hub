/**
 * 文件预览弹窗（2026-10-03，用户要求"图片缩略图/预览"；当天补齐视频/音频）。
 *
 * 能直接看的：图片（`<img>`）、PDF（`<iframe>`）、视频/音频（`<video>`/`<audio>`，都走一次性票据，
 * 视频拖动进度依赖服务端的 Range 支持）、文本（带 Token 读回来，超过上限只显示开头）。
 * 看不了的（Word/Excel/PPT、压缩包、mov/mkv 这类浏览器不认的视频容器）：
 * 明说看不了 + **告诉用户下一步怎么做**（下载后用本机软件打开），
 * ⛔ 不要假装能打开，也不要摆一个放不出来的播放器（AGENTS §14.1：不会用 = 缺陷）。
 */
import { useEffect, useRef, useState } from 'react';
import { Alert, Button, Space, Spin, Typography } from 'antd';
import { DownloadOutlined } from '@ant-design/icons';

import { ResponsiveSheet } from './ResponsiveSheet';
import {
  canPlayInBrowser,
  fetchPreviewUrl,
  fetchTextPreview,
  previewHint,
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
  const playable = canPlayInBrowser(mimeType, fileName);
  const [url, setUrl] = useState('');
  const [text, setText] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [mediaError, setMediaError] = useState('');
  /** 重新取票据用（票据只有 60 秒，看长视频中途会过期） */
  const [reloadToken, setReloadToken] = useState(0);
  /** 记住播放位置，重取票据后接着看 */
  const positionRef = useRef(0);

  useEffect(() => {
    if (!open) {
      setUrl('');
      setText('');
      setError('');
      setMediaError('');
      positionRef.current = 0;
      return;
    }
    let cancelled = false;
    setLoading(true);
    setError('');
    setMediaError('');
    // 需要"一个能直接放进 src 的地址"的类型：图片、PDF、视频、音频
    const needsUrl =
      kind === 'image' || kind === 'pdf' || ((kind === 'video' || kind === 'audio') && playable);
    const task =
      kind === 'text'
        ? fetchTextPreview(fileId).then((content) => {
            if (!cancelled) setText(content);
          })
        : needsUrl
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
  }, [open, fileId, kind, playable, reloadToken]);

  /** 媒体加载失败：多半是票据过期了（60 秒），给用户一个「重新加载」而不是干瞪眼 */
  function handleMediaError() {
    setMediaError('播放中断了（预览链接只有 60 秒有效期）。点「重新加载」接着看，进度会保留。');
  }

  function renderMedia() {
    if (mediaError) {
      return (
        <Alert
          type="warning"
          showIcon
          message={mediaError}
          action={
            <Button size="small" onClick={() => setReloadToken((token) => token + 1)}>
              重新加载
            </Button>
          }
        />
      );
    }
    const mediaProps = {
      controls: true,
      autoPlay: true,
      playsInline: true,
      onTimeUpdate: (event: React.SyntheticEvent<HTMLMediaElement>) => {
        positionRef.current = event.currentTarget.currentTime;
      },
      onLoadedMetadata: (event: React.SyntheticEvent<HTMLMediaElement>) => {
        if (positionRef.current > 0 && Number.isFinite(event.currentTarget.duration)) {
          event.currentTarget.currentTime = Math.min(
            positionRef.current,
            Math.max(0, event.currentTarget.duration - 1),
          );
        }
      },
      onError: handleMediaError,
    };
    if (kind === 'video') {
      return (
        // eslint-disable-next-line jsx-a11y/media-has-caption
        <video
          {...mediaProps}
          src={url}
          preload="metadata"
          style={{ width: '100%', maxHeight: '70vh', background: '#000', borderRadius: 6 }}
        />
      );
    }
    // eslint-disable-next-line jsx-a11y/media-has-caption
    return <audio {...mediaProps} src={url} style={{ width: '100%' }} />;
  }

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
    // 视频/音频：浏览器直接播（服务端支持 Range，所以能拖进度）
    if ((kind === 'video' || kind === 'audio') && playable && url) {
      return renderMedia();
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
      <Typography.Paragraph type="secondary" style={{ marginBottom: 0 }}>
        {previewHint(kind, fileName)}
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
