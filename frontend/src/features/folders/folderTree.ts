/**
 * 左侧目录树（2026-10-03 用户要求：像电脑上的"文件资源管理器"那样看结构）。
 *
 * 纯逻辑放这里，UI 在 `components/FolderTree.tsx`：
 * 树是**按需展开**的（折叠的层级不发请求），所以只需要"哪些节点该展开"这几个小函数。
 */
import type { ListChildrenParams } from '../../services/foldersApi';
import type { Folder } from '../../types/folder';

/** 树里每一层取多少个子目录（目录数超过就提示，不做分页 —— 目录树分页没法用） */
export const TREE_PAGE_SIZE = 100;

/** 树每层用的查询参数（与列表页的 key 区分开，互不干扰） */
export const TREE_PARAMS: ListChildrenParams = {
  page_size: TREE_PAGE_SIZE,
  sort_by: 'name',
  sort_order: 'asc',
};

/** 根目录在界面上叫「我的文件」（与面包屑、导航保持一致） */
export function treeNodeLabel(folder: Pick<Folder, 'name' | 'is_root'>): string {
  return folder.is_root ? '我的文件' : folder.name;
}

/**
 * 当前路径上**需要展开**的节点：面包屑里除最后一个（当前目录自身）之外的 id。
 * 进到深层目录时左侧树要自动打开到那一层，否则用户看不到自己在哪。
 */
export function ancestorIds(chain: Array<{ id: string }>): string[] {
  return chain.slice(0, Math.max(0, chain.length - 1)).map((folder) => folder.id);
}

/** 合并展开集合：只加不减（用户手动展开的层级不能因为导航被收起来） */
export function withExpanded(current: string[], ids: string[]): string[] {
  const merged = new Set(current);
  for (const id of ids) {
    merged.add(id);
  }
  return [...merged];
}

/** 点开/收起一个节点 */
export function toggleExpanded(current: string[], id: string): string[] {
  return current.includes(id) ? current.filter((item) => item !== id) : [...current, id];
}

/** 这一层还有没显示出来的子目录（超过一页时给个提示，别让用户以为丢了） */
export function hiddenChildrenCount(listing: { total: number; items: unknown[] } | undefined): number {
  if (!listing) return 0;
  return Math.max(0, listing.total - listing.items.length);
}
