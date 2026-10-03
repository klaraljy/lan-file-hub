/**
 * 一行的操作按钮（2026-10-03，用户投诉"按钮颜色和比例不舒服"之后的统一实现）。
 *
 * 规则与顺序都在 `features/files/rowActions.ts`（纯函数 + 单测），这里只负责画：
 *   行上 = 一个主按钮 + 一个次要按钮 + 「更多」下拉；
 *   桌面端 small（紧凑、不抢戏），手机端 44px 高（点得准）。
 *
 * ⛔ 不要在这里各写一套按钮 —— 三个视图（表格 / 手机卡片 / 网格）必须长得一样。
 */
import { Button, Dropdown, Space, Tooltip } from 'antd';
import type { MenuProps } from 'antd';
import {
  DeleteOutlined,
  DownloadOutlined,
  EditOutlined,
  EyeOutlined,
  FolderOpenOutlined,
  HistoryOutlined,
  InfoCircleOutlined,
  MoreOutlined,
  SendOutlined,
  ShareAltOutlined,
  TeamOutlined,
} from '@ant-design/icons';

import { rowActions, type RowAction, type RowActionKey, type RowKind } from '../features/files/rowActions';
import { TOUCH_HEIGHT } from '../hooks/useIsMobile';

const ICONS: Record<RowActionKey, React.ReactNode> = {
  open: <FolderOpenOutlined />,
  download: <DownloadOutlined />,
  preview: <EyeOutlined />,
  detail: <InfoCircleOutlined />,
  versions: <HistoryOutlined />,
  send: <SendOutlined />,
  share: <ShareAltOutlined />,
  permission: <TeamOutlined />,
  rename: <EditOutlined />,
  delete: <DeleteOutlined />,
};

interface Props {
  kind: RowKind;
  canPreview?: boolean;
  /** 手机 / 桌面：只影响尺寸与次按钮的排版 */
  mobile?: boolean;
  /** 主按钮是否占满整行（手机卡片上是这样，手指好点） */
  blockPrimary?: boolean;
  /**
   * 主按钮的"分量"（2026-10-03 用户投诉"颜色不舒服"）：
   *   - `ghost`：桌面表格/网格用 —— 描边款，一屏几十行也不会变成一大片蓝色；
   *   - `solid`：手机卡片用 —— 一屏一张卡，实心更清楚。
   */
  tone?: 'ghost' | 'solid';
  onAction: (key: RowActionKey) => void;
}

export function RowActions({
  kind,
  canPreview,
  mobile,
  blockPrimary,
  tone = 'ghost',
  onAction,
}: Props) {
  const set = rowActions(kind, { canPreview });
  const [primary, secondary] = set.inline;
  const size = mobile ? ('middle' as const) : ('small' as const);
  const heightStyle = mobile ? { height: TOUCH_HEIGHT } : undefined;

  const menu: MenuProps['items'] = set.menu.map((action) => ({
    key: action.key,
    icon: ICONS[action.key],
    label: action.label,
    danger: action.danger,
  }));

  function button(action: RowAction, type: 'primary' | 'default', block = false) {
    return (
      <Button
        key={action.key}
        type={type}
        ghost={type === 'primary' && tone === 'ghost'}
        size={size}
        block={block}
        icon={ICONS[action.key]}
        style={heightStyle}
        onClick={(event) => {
          event.stopPropagation();
          onAction(action.key);
        }}
      >
        {action.label}
      </Button>
    );
  }

  return (
    <Space size={8} style={{ width: blockPrimary ? '100%' : undefined }} wrap={!blockPrimary}>
      {primary && button(primary, 'primary', blockPrimary)}
      <Space size={8}>
        {secondary && button(secondary, 'default')}
        <Dropdown
          trigger={['click']}
          menu={{
            items: menu,
            onClick: ({ key, domEvent }) => {
              domEvent.stopPropagation();
              onAction(key as RowActionKey);
            },
          }}
        >
          <Tooltip title="更多操作">
            <Button
              size={size}
              icon={<MoreOutlined />}
              aria-label="更多操作"
              style={heightStyle}
              onClick={(event) => event.stopPropagation()}
            />
          </Tooltip>
        </Dropdown>
      </Space>
    </Space>
  );
}
