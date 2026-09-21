/**
 * 分享链接展示块：**二维码 + 链接 + 复制**（AGENTS.md §14.4，用户 2026-09-21 明确要求"加二维码"）。
 *
 * 为什么要二维码：用户要把文件给旁边的人（或自己的另一台设备）时，
 * "复制链接 → 发微信 → 对方点开"这一串太长；直接扫一下最快。
 * 手机自带相机、微信/QQ 的「扫一扫」都能识别，**不需要登录**（分享页是公开页）。
 *
 * 二维码内容就是链接本身（`http://<局域网IP>:5173/s/<token>`），
 * 所以这里不做任何额外编码——扫出来就是能直接打开的地址。
 */
import { Alert, Button, Input, Space, Typography } from 'antd';
import { CopyOutlined } from '@ant-design/icons';
import { QRCodeSVG } from 'qrcode.react';

interface Props {
  /** 要展示与编码的完整链接 */
  url: string;
  /** 地址提示（例如"你正用 127.0.0.1 打开，已自动换成局域网地址"） */
  warning?: string;
  /** 链接下方的小字说明（例如"关闭弹窗后无法再次查看"） */
  notice?: string;
  /** 复制回调：由父组件复用自己那套剪贴板逻辑 */
  onCopy: (text: string) => void;
  /** 二维码边长，默认 168；手机上也能扫得动 */
  size?: number;
}

export function ShareLinkBox({ url, warning, notice, onCopy, size = 168 }: Props) {
  return (
    <Space direction="vertical" size={10} style={{ width: '100%' }}>
      {warning && <Alert type="warning" showIcon message={warning} />}

      <div
        data-testid="share-qr"
        style={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          gap: 4,
          padding: 8,
          background: '#fff',
          border: '1px solid #f0f0f0',
          borderRadius: 8,
        }}
      >
        {/* 白底 + quiet zone：保证手机能扫出来 */}
        <QRCodeSVG value={url} size={size} level="M" marginSize={2} bgColor="#ffffff" fgColor="#000000" />
        <Typography.Text type="secondary" style={{ fontSize: 12 }}>
          用手机相机或微信「扫一扫」直接打开
        </Typography.Text>
      </div>

      <Input
        readOnly
        value={url}
        addonAfter={
          <Button type="link" size="small" icon={<CopyOutlined />} onClick={() => onCopy(url)}>
            复制链接
          </Button>
        }
      />

      {notice && (
        <Typography.Text type="secondary" style={{ fontSize: 12 }}>
          {notice}
        </Typography.Text>
      )}
    </Space>
  );
}
