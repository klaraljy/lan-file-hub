/**
 * 列表里每一行该有哪些操作（2026-10-03，用户投诉"按钮颜色和比例不舒服、每行一堆按钮"）。
 *
 * 问题现场：桌面表格一行摆 7 个 `type="link"` 文字按钮 + 1 个蓝色主按钮，
 * 一屏几十行 = 一大片蓝字配一堆蓝底按钮，手机上更是"只有一个巨大的「发送给…」，下载反而找不到"。
 *
 * 统一规则（这一份是唯一事实来源，三个视图都照它渲染）：
 *   1. 行上**最多两个按钮**：第一个是主按钮（蓝色，一行只有这一个），第二个是次要按钮；
 *   2. 其余全部进「更多」下拉，按使用频率排序，破坏性操作放最后并标红；
 *   3. 「发送给…」必须留在行上（AGENTS §14.3 用户明确要求不许藏进"更多"），
 *      但不再抢主按钮的位置 —— 主按钮给这一行最常用的动作（文件=下载，文件夹=打开）。
 *
 * 纯函数，单测覆盖（按钮多了少了都是用户直接看得见的问题）。
 */

export type RowKind = 'file' | 'folder';

export type RowActionKey =
  | 'open'
  | 'download'
  | 'preview'
  | 'detail'
  | 'versions'
  | 'send'
  | 'share'
  | 'permission'
  | 'rename'
  | 'delete';

export interface RowAction {
  key: RowActionKey;
  label: string;
  /** 破坏性操作（下拉里显示红色，并且必须二次确认） */
  danger?: boolean;
}

export interface RowActionSet {
  /** 直接摆在行上的按钮：**第一个是主按钮**，最多两个 */
  inline: RowAction[];
  /** 收进「更多」下拉的 */
  menu: RowAction[];
}

export interface RowActionOptions {
  /** 这个文件能不能在网页里直接预览（图片/视频/PDF/文本）—— 不能就不显示「预览」 */
  canPreview?: boolean;
}

const SEND: RowAction = { key: 'send', label: '发送给…' };
const SHARE: RowAction = { key: 'share', label: '分享' };
const PERMISSION: RowAction = { key: 'permission', label: '权限' };
const DELETE: RowAction = { key: 'delete', label: '删除', danger: true };

/** 文件：主按钮＝下载（用户 2026-10-03 明确要"我的下载按钮呢"） */
export function fileRowActions(options: RowActionOptions = {}): RowActionSet {
  const menu: RowAction[] = [];
  if (options.canPreview) {
    menu.push({ key: 'preview', label: '预览' });
  }
  menu.push(
    { key: 'detail', label: '详情' },
    { key: 'versions', label: '版本历史' },
    SHARE,
    PERMISSION,
    DELETE,
  );
  return {
    inline: [{ key: 'download', label: '下载' }, SEND],
    menu,
  };
}

/** 文件夹：主按钮＝打开；「重命名」不再单独占一个行内按钮 */
export function folderRowActions(): RowActionSet {
  return {
    inline: [{ key: 'open', label: '打开' }, SEND],
    menu: [{ key: 'rename', label: '重命名' }, SHARE, PERMISSION, DELETE],
  };
}

export function rowActions(
  kind: RowKind,
  options: RowActionOptions = {},
): RowActionSet {
  return kind === 'file' ? fileRowActions(options) : folderRowActions();
}

/** 主按钮（行上第一个） */
export function primaryAction(set: RowActionSet): RowAction | undefined {
  return set.inline[0];
}

/** 行上的次要按钮（第二个，没有就没有） */
export function secondaryAction(set: RowActionSet): RowAction | undefined {
  return set.inline[1];
}
