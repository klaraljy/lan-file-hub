/**
 * 「发文件给某人」选择器的纯逻辑（2026-10-03）。
 *
 * 用户实测的抱怨：「点击发文件，怎么不能按文件夹的形式发送，然后得到文件再在里面下载呢」——
 * 原来的选择器只列**根目录下的文件**，既选不了文件夹，也进不去子目录。
 *
 * 现在把目录与文件合成一份可点列表（文件夹能进去、也能整个发出去），
 * 这里放"合并 + 过滤 + 面包屑文案"这些可单测的部分。
 */
import type { FileMeta } from '../../types/file';
import type { Folder } from '../../types/folder';

export interface PickerRow {
  kind: 'folder' | 'file';
  id: string;
  name: string;
  /** 文件才有大小；文件夹是 0 */
  sizeBytes: number;
}

/** 目录在前、文件在后（与「我的文件」列表一致），各自保持传入顺序 */
export function mergePickerRows(folders: Folder[], files: FileMeta[]): PickerRow[] {
  const folderRows: PickerRow[] = folders.map((folder) => ({
    kind: 'folder',
    id: folder.id,
    name: folder.name,
    sizeBytes: 0,
  }));
  const fileRows: PickerRow[] = files.map((file) => ({
    kind: 'file',
    id: file.id,
    name: file.name,
    sizeBytes: file.size_bytes,
  }));
  return [...folderRows, ...fileRows];
}

/** 按名字过滤（只在当前这一层里过滤，不递归 —— 与界面上的提示一致） */
export function filterPickerRows(rows: PickerRow[], keyword: string): PickerRow[] {
  const trimmed = keyword.trim().toLowerCase();
  if (!trimmed) return rows;
  return rows.filter((row) => row.name.toLowerCase().includes(trimmed));
}

/** 当前在哪个目录（给提示文案用）：根目录显示「我的文件」 */
export function pickerLocationLabel(chain: Folder[]): string {
  if (chain.length === 0) return '我的文件';
  const names = chain.map((folder) => (folder.is_root ? '我的文件' : folder.name));
  return names.join(' / ');
}

/** 发送成功后的提示：文件夹要说明"对方点开就能看到里面的东西" */
export function sentMessage(row: PickerRow, who: string): string {
  if (row.kind === 'folder') {
    return `已把文件夹「${row.name}」发给 ${who}：他/她在「分享给我的」里点开这个文件夹，就能看到并下载里面的文件`;
  }
  return `已把「${row.name}」发给 ${who}：他/她登录后在「分享给我的」里就能看到并下载`;
}
