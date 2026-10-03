import { describe, expect, it } from 'vitest';

import {
  TREE_PAGE_SIZE,
  ancestorIds,
  hiddenChildrenCount,
  toggleExpanded,
  treeNodeLabel,
  withExpanded,
} from '../src/features/folders/folderTree';

/**
 * 左侧目录树（2026-10-03 用户要求"像资源管理器那样看结构"）。
 *
 * 这里钉的是"该展开哪几层、根目录叫什么、一层放不下时怎么提示"：
 * 算错了要么用户看不到自己在哪一层，要么以为文件夹丢了。
 */
describe('treeNodeLabel', () => {
  it('根目录显示成「我的文件」，与面包屑/导航一致', () => {
    expect(treeNodeLabel({ name: '', is_root: true })).toBe('我的文件');
    expect(treeNodeLabel({ name: '原版', is_root: false })).toBe('原版');
  });
});

describe('ancestorIds', () => {
  it('返回链路上除当前目录自身以外的 id（这些层要展开）', () => {
    expect(ancestorIds([{ id: 'root' }, { id: 'a' }, { id: 'b' }])).toEqual(['root', 'a']);
  });

  it('空链路与只有根目录时都不展开任何东西', () => {
    expect(ancestorIds([])).toEqual([]);
    expect(ancestorIds([{ id: 'root' }])).toEqual([]);
  });
});

describe('withExpanded / toggleExpanded', () => {
  it('只加不减：导航不会把用户手动展开的层收起来', () => {
    expect(withExpanded(['x'], ['root', 'a'])).toEqual(['x', 'root', 'a']);
  });

  it('不重复添加已经展开的层', () => {
    expect(withExpanded(['root', 'a'], ['a'])).toEqual(['root', 'a']);
  });

  it('点击可以展开也可以收起', () => {
    expect(toggleExpanded([], 'a')).toEqual(['a']);
    expect(toggleExpanded(['a', 'b'], 'a')).toEqual(['b']);
  });
});

describe('hiddenChildrenCount', () => {
  it('一层超过上限时要说清少了多少个', () => {
    expect(hiddenChildrenCount({ total: 130, items: new Array(TREE_PAGE_SIZE).fill({}) })).toBe(30);
  });

  it('没超出或还没加载时都是 0', () => {
    expect(hiddenChildrenCount({ total: 3, items: [{}, {}, {}] })).toBe(0);
    expect(hiddenChildrenCount(undefined)).toBe(0);
  });
});
