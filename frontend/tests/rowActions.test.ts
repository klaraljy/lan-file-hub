import { describe, expect, it } from 'vitest';

import {
  fileRowActions,
  folderRowActions,
  primaryAction,
  rowActions,
  secondaryAction,
} from '../src/features/files/rowActions';

/**
 * 一行该有哪些按钮（2026-10-03 用户投诉"每行一堆按钮、颜色和比例不舒服"）。
 *
 * 这里钉的是三条用户直接看得见的规则：
 *   ① 行上最多两个按钮（多一个就又是一堆）；
 *   ② 主按钮是这一行最常用的动作（文件=下载、文件夹=打开）；
 *   ③ 「发送给…」必须在行上（AGENTS §14.3 不许藏进"更多"），但只当次要按钮。
 */
describe('fileRowActions', () => {
  it('行上就两个：下载（主）+ 发送给…（次）', () => {
    const set = fileRowActions();
    expect(set.inline.map((action) => action.key)).toEqual(['download', 'send']);
    expect(primaryAction(set)?.key).toBe('download');
    expect(secondaryAction(set)?.key).toBe('send');
  });

  it('其余动作进「更多」，删除放最后且标红', () => {
    const set = fileRowActions();
    const keys = set.menu.map((action) => action.key);
    expect(keys).toEqual(['detail', 'versions', 'share', 'permission', 'delete']);
    expect(set.menu[set.menu.length - 1]!.danger).toBe(true);
    // 「发送给…」绝不能在菜单里（§14.3）
    expect(keys).not.toContain('send');
  });

  it('不能预览的文件不显示「预览」，能预览的才加进去', () => {
    expect(fileRowActions().menu.map((action) => action.key)).not.toContain('preview');
    expect(fileRowActions({ canPreview: true }).menu[0]!.key).toBe('preview');
  });
});

describe('folderRowActions', () => {
  it('文件夹的主按钮是「下载」（下载里面的东西，2026-10-03 用户要求），第二个是「打开」', () => {
    const set = folderRowActions();
    expect(set.inline.map((action) => action.key)).toEqual(['downloadAll', 'open', 'send']);
    expect(primaryAction(set)?.key).toBe('downloadAll');
    expect(primaryAction(set)?.label).toBe('下载');
  });

  it('重命名不再单独占一个行内按钮，进「更多」', () => {
    const set = folderRowActions();
    expect(set.inline.map((action) => action.key)).not.toContain('rename');
    expect(set.menu.map((action) => action.key)).toContain('rename');
    expect(set.menu.map((action) => action.key)).toEqual([
      'rename',
      'share',
      'permission',
      'delete',
    ]);
  });

  it('文件夹没有单文件那种下载/预览/版本动作', () => {
    const keys = folderRowActions().menu.map((action) => action.key);
    expect(keys).not.toContain('download');
    expect(keys).not.toContain('preview');
    expect(keys).not.toContain('versions');
  });
});

describe('全局规则', () => {
  it('文件行最多两个行内按钮，文件夹行最多三个（打开 + 下载 + 发送给…）', () => {
    expect(rowActions('file').inline.length).toBeLessThanOrEqual(2);
    expect(rowActions('file', { canPreview: true }).inline.length).toBeLessThanOrEqual(2);
    expect(rowActions('folder').inline.length).toBeLessThanOrEqual(3);
  });

  it('每行都恰好有一个主按钮（不能有零个，也不能有两个抢）', () => {
    for (const kind of ['file', 'folder'] as const) {
      const set = rowActions(kind);
      expect(set.inline.filter((action) => action.key === primaryAction(set)?.key)).toHaveLength(1);
    }
  });
});
