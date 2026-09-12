/**
 * 文件列表的视图逻辑：把「文件夹 + 文件」两种数据合成一张表、排序、按名称过滤、
 * 以及在列表/网格两种视图之间切换所需的纯计算。
 *
 * 全部是纯函数：不碰网络、不碰 DOM，直接单测。
 *
 * 关于服务端能力的边界（重要）：
 *   当前列表接口只返回一页数据（分页控件属于后续阶段），所以这里的排序与搜索
 *   都是**针对当前已加载的这一页**做的。真正的服务端排序白名单与全文搜索
 *   等后端提供对应参数后再接（docs/api.md §分页与排序）。
 */
import type { FileMeta } from '../../types/file';
import type { Folder } from '../../types/folder';

export type SortField = 'name' | 'size' | 'updated_at';
export type SortOrder = 'asc' | 'desc';
export type ViewMode = 'list' | 'grid';

export interface SortState {
  field: SortField;
  order: SortOrder;
}

export const SORT_FIELD_LABELS: Record<SortField, string> = {
  name: '名称',
  size: '大小',
  updated_at: '修改时间',
};

/** 排序菜单的字段顺序（也是唯一允许出现在菜单里的字段集合） */
export const SORT_FIELDS: readonly SortField[] = ['name', 'size', 'updated_at'];

/** 默认：按名称升序（与目录树习惯一致） */
export const DEFAULT_SORT: SortState = { field: 'name', order: 'asc' };

/** 参与排序的行必须有的字段 */
export interface SortableRow {
  kind: 'folder' | 'file';
  name: string;
  size_bytes: number;
  updated_at: string;
}

/** 文件列表里的一行：文件夹与文件合并展示，用 kind 区分 */
export type BrowseRow =
  | ({ key: string; kind: 'folder'; folder: Folder } & SortableRow)
  | ({ key: string; kind: 'file'; file: FileMeta } & SortableRow & { content_hash: string });

/** 把两个接口的结果合成一份可排序的列表；文件夹在前，文件在后 */
export function buildBrowseRows(folders: Folder[], files: FileMeta[]): BrowseRow[] {
  const folderRows: BrowseRow[] = folders.map((folder) => ({
    key: `folder-${folder.id}`,
    kind: 'folder',
    name: folder.name,
    size_bytes: 0,
    updated_at: folder.updated_at,
    folder,
  }));
  const fileRows: BrowseRow[] = files.map((file) => ({
    key: `file-${file.id}`,
    kind: 'file',
    name: file.name,
    size_bytes: file.size_bytes,
    updated_at: file.updated_at,
    content_hash: file.content_hash,
    file,
  }));
  return [...folderRows, ...fileRows];
}

function compareName(a: string, b: string): number {
  // 中文按拼音、数字按数值，避免「文件10」排在「文件2」前面
  return a.localeCompare(b, 'zh-CN', { numeric: true, sensitivity: 'base' });
}

function compareRows(a: SortableRow, b: SortableRow, sort: SortState): number {
  let result = 0;
  switch (sort.field) {
    case 'size':
      result = a.size_bytes - b.size_bytes;
      break;
    case 'updated_at': {
      const left = Date.parse(a.updated_at);
      const right = Date.parse(b.updated_at);
      // 时间解析失败时退化成按名称比较，避免 NaN 让排序结果随机
      result = Number.isNaN(left) || Number.isNaN(right) ? compareName(a.name, b.name) : left - right;
      break;
    }
    case 'name':
    default:
      result = compareName(a.name, b.name);
      break;
  }
  if (result === 0) {
    // 同值时用名称兜底，保证排序结果稳定可预期
    result = compareName(a.name, b.name);
  }
  return sort.order === 'desc' ? -result : result;
}

/**
 * 排序：**文件夹永远排在文件前面**（文件管理器的习惯），
 * 两组内部再按所选字段排序。
 */
export function sortRows<T extends SortableRow>(rows: T[], sort: SortState): T[] {
  const folders = rows.filter((row) => row.kind === 'folder').sort((a, b) => compareRows(a, b, sort));
  const files = rows.filter((row) => row.kind === 'file').sort((a, b) => compareRows(a, b, sort));
  return [...folders, ...files];
}

/** 按名称过滤（不区分大小写、忽略首尾空格）；关键字为空时原样返回 */
export function filterRows<T extends { name: string }>(rows: T[], keyword: string): T[] {
  const trimmed = keyword.trim().toLowerCase();
  if (!trimmed) return rows;
  return rows.filter((row) => row.name.toLowerCase().includes(trimmed));
}

/**
 * 点击某个排序字段时的下一个排序状态：
 * 同一个字段 → 在升/降序之间切换；换字段 → 名称默认升序，其余默认降序
 * （大小与时间通常是「先看大的/新的」）。
 */
export function nextSortState(current: SortState, field: SortField): SortState {
  if (current.field === field) {
    return { field, order: current.order === 'asc' ? 'desc' : 'asc' };
  }
  return { field, order: field === 'name' ? 'asc' : 'desc' };
}

/** 排序菜单里显示的箭头后缀 */
export function sortIndicator(sort: SortState, field: SortField): string {
  if (sort.field !== field) return '';
  return sort.order === 'asc' ? ' ↑' : ' ↓';
}

/** 人类可读的字节数；0 显示 0 B（文件夹用 — 由调用方决定） */
export function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

/** 文件类型标签：优先用 MIME，缺失时按扩展名兜底 */
export function typeLabel(file: FileMeta): string {
  if (file.mime_type) return file.mime_type;
  const dot = file.name.lastIndexOf('.');
  if (dot <= 0 || dot === file.name.length - 1) return '未知类型';
  return file.name.slice(dot + 1).toUpperCase();
}
