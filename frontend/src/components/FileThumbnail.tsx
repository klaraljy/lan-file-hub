/**
 * 图片缩略图（2026-10-03）。
 *
 * 只有图片才去换票据、加载小图；其它类型渲染一个图标占位，
 * **不发起任何请求**（列表里几十个文件，不能每个都白跑一趟）。
 *
 * 票据 60 秒有效：缩略图是"进页面就加载"的，够用；页面开着很久之后图片失效也不影响
 * 使用（点开预览会重新换一张新票据）。
 */
import { useEffect, useState } from 'react';
import { FileImageOutlined } from '@ant-design/icons';

import { canPreview, fetchPreviewUrl, previewKind } from '../services/filePreview';

interface Props {
  fileId: string;
  fileName: string;
  mimeType?: string;
  /** 边长（正方形），手机上 44 就够看清 */
  size?: number;
}

export function FileThumbnail({ fileId, fileName, mimeType, size = 44 }: Props) {
  const kind = previewKind(mimeType, fileName);
  const [url, setUrl] = useState('');

  useEffect(() => {
    if (kind !== 'image') {
      setUrl('');
      return;
    }
    let cancelled = false;
    fetchPreviewUrl(fileId)
      .then((address) => {
        if (!cancelled) setUrl(address);
      })
      .catch(() => {
        // 缩略图失败不打扰用户：预览/下载仍然可用
      });
    return () => {
      cancelled = true;
    };
  }, [fileId, kind]);

  const box: React.CSSProperties = {
    width: size,
    height: size,
    borderRadius: 6,
    background: '#f5f5f5',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    overflow: 'hidden',
    flex: '0 0 auto',
  };

  if (kind === 'image' && url) {
    return (
      <span style={box} data-testid="file-thumbnail">
        <img
          src={url}
          alt={fileName}
          loading="lazy"
          style={{ width: '100%', height: '100%', objectFit: 'cover' }}
        />
      </span>
    );
  }

  return (
    <span style={box} data-testid="file-thumbnail-placeholder">
      <FileImageOutlined style={{ color: canPreview(mimeType, fileName) ? '#8c8c8c' : '#d9d9d9' }} />
    </span>
  );
}
