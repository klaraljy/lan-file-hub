/**
 * 手机上用的通用条目标题卡（2026-10-03）。
 *
 * 为什么要有它：用户实测反馈手机端"太潦草" —— 起因是「分享给我的」「回收站」等页面
 * 直接用了电脑版**宽表格**，在 412px 宽的手机上表头被挤成竖排（"名 称 / 类 型 / 大 小"），
 * 而且表格比屏幕宽 → 页面横向溢出 → 底栏在横滑时被推出可视区（用户报的"导航栏消失"）。
 *
 * 规则（AGENTS.md §14.1）：
 *   - 一条一张卡：图标 + 名称 + 一行次要信息 + 一行操作按钮；
 *   - 主操作是**整行大按钮**（手指点得准）；破坏性操作放右侧、用 danger 色；
 *   - 卡片本身可点（进入/查看），按钮区阻止冒泡，避免误触。
 */
import type { ReactNode } from 'react';
import { Card, Typography } from 'antd';

import { TOUCH_HEIGHT } from '../hooks/useIsMobile';

interface Props {
  icon: ReactNode;
  title: string;
  /** 次要信息（大小 / 时间 / 权限…），约定用 ` · ` 分隔 */
  subtitle?: ReactNode;
  /** 右上角的标签（权限、状态…） */
  extra?: ReactNode;
  /** 操作区：一般是一到两个按钮，主按钮请给 `block` */
  actions?: ReactNode;
  onClick?: () => void;
}

export function MobileItemCard({ icon, title, subtitle, extra, actions, onClick }: Props) {
  return (
    <Card
      size="small"
      hoverable={Boolean(onClick)}
      styles={{ body: { padding: 12 } }}
      onClick={onClick}
      style={{ width: '100%' }}
    >
      <div style={{ display: 'flex', gap: 10, alignItems: 'flex-start' }}>
        <span style={{ fontSize: 22, lineHeight: '26px' }}>{icon}</span>
        <div style={{ flex: 1, minWidth: 0 }}>
          <Typography.Text strong ellipsis={{ tooltip: title }} style={{ display: 'block', fontSize: 16 }}>
            {title}
          </Typography.Text>
          {subtitle && (
            <Typography.Text type="secondary" style={{ fontSize: 12 }}>
              {subtitle}
            </Typography.Text>
          )}
        </div>
        {extra}
      </div>

      {actions && (
        <div
          style={{ display: 'flex', gap: 8, marginTop: 10, alignItems: 'center' }}
          onClick={(event) => event.stopPropagation()}
        >
          {actions}
        </div>
      )}
    </Card>
  );
}

/** 卡片列表容器：统一间距，并给底部悬浮条/导航留位置 */
export function MobileCardList({ children }: { children: ReactNode }) {
  return <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>{children}</div>;
}

/** 主按钮的统一高度（手指友好），与 FileCardList 保持一致 */
export const CARD_ACTION_HEIGHT = TOUCH_HEIGHT;
