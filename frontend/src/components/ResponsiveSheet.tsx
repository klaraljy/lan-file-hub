/**
 * 手机 / 桌面统一的弹层（AGENTS.md §15 阶段 ②："弹窗在手机上改成底部滑出"）。
 *
 * 手机上（< lg）用 **底部滑出的 Drawer**：内容占满宽度、接近全屏，按钮大、间距松，
 * 一只手能够到底部的主按钮；桌面端仍是居中 Modal，宽度与行为跟改造前一致。
 *
 * 这样各弹窗（发送给… / 分享 / 权限 / 版本历史 / 新建文件夹）只写一份内容，
 * 不用每个都写两套布局，也避免"手机版漏改一个"。
 *
 * ⛔ 不要绕过这个组件去直接写 Mobile 判断：`发送给…` 与 `分享` 必须继续用
 * `ShareLinkBox`（带二维码，AGENTS.md §14.4），「发送给…」必须仍是显眼主按钮（§14.3）。
 */
import type { ReactNode } from 'react';
import { Drawer, Modal } from 'antd';

import { useIsMobile } from '../hooks/useIsMobile';

interface Props {
  open: boolean;
  title: ReactNode;
  onClose: () => void;
  children: ReactNode;
  /** 桌面端弹窗宽度（手机端占满宽度，忽略这个值） */
  desktopWidth?: number;
  /** 底部固定区域（手机端放在滑出面板底部；桌面端放在 Modal footer） */
  footer?: ReactNode;
  /** 手机端滑出面板高度，默认 90% */
  mobileHeight?: string;
}

export function ResponsiveSheet({
  open,
  title,
  onClose,
  children,
  desktopWidth = 640,
  footer,
  mobileHeight = '90%',
}: Props) {
  const isMobile = useIsMobile();

  if (isMobile) {
    return (
      <Drawer
        open={open}
        placement="bottom"
        height={mobileHeight}
        title={title}
        onClose={onClose}
        destroyOnClose
        footer={footer}
        styles={{
          header: { paddingInline: 12, paddingBlock: 10 },
          body: { padding: 12, paddingBottom: footer ? 12 : 24 },
          footer: { padding: 12 },
        }}
      >
        {children}
      </Drawer>
    );
  }

  return (
    <Modal
      open={open}
      title={title}
      onCancel={onClose}
      footer={footer ?? null}
      width={desktopWidth}
      destroyOnClose
    >
      {children}
    </Modal>
  );
}
