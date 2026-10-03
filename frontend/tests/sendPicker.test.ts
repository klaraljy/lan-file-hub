import { describe, expect, it } from 'vitest';

import {
  filterPickerRows,
  mergePickerRows,
  pickerLocationLabel,
  sentMessage,
  type PickerRow,
} from '../src/features/sharing/sendPicker';
import type { FileMeta } from '../src/types/file';
import type { Folder } from '../src/types/folder';

/**
 * 「发文件给某人」选择器（2026-10-03 用户要求：能按**文件夹**发，对方进去再下载）。
 *
 * 这里钉的是"合并与过滤"：文件夹排前面、过滤不递归、根目录叫「我的文件」。
 * 合并错了用户会在错的目录里找东西，过滤错了会以为文件不存在。
 */
function folder(id: string, name: string, isRoot = false): Folder {
  return {
    id,
    name,
    parent_id: isRoot ? null : 'parent',
    owner_id: 'user-1',
    is_root: isRoot,
    created_at: '2026-01-01T00:00:00Z',
    updated_at: '2026-01-01T00:00:00Z',
  } as Folder;
}

function file(id: string, name: string, size: number): FileMeta {
  return {
    id,
    owner_id: 'user-1',
    folder_id: 'folder-1',
    current_version_id: 'v1',
    name,
    mime_type: 'video/mp4',
    size_bytes: size,
    content_hash: 'a'.repeat(64),
    status: 'ACTIVE',
    created_at: '2026-01-01T00:00:00Z',
    updated_at: '2026-01-01T00:00:00Z',
    deleted_at: null,
  } as FileMeta;
}

describe('mergePickerRows', () => {
  it('文件夹排在文件前面，各自保持原顺序', () => {
    const rows = mergePickerRows(
      [folder('f1', '原版'), folder('f2', 'IF线')],
      [file('x1', '1.mp4', 100), file('x2', '2.mp4', 200)],
    );

    expect(rows.map((row) => `${row.kind}:${row.name}`)).toEqual([
      'folder:原版',
      'folder:IF线',
      'file:1.mp4',
      'file:2.mp4',
    ]);
    expect(rows[2]!.sizeBytes).toBe(100);
    expect(rows[0]!.sizeBytes).toBe(0);
  });

  it('只有文件或只有文件夹时也正常', () => {
    expect(mergePickerRows([], [file('x', 'a.mp4', 1)])).toHaveLength(1);
    expect(mergePickerRows([folder('f', 'dir')], [])).toHaveLength(1);
    expect(mergePickerRows([], [])).toEqual([]);
  });
});

describe('filterPickerRows', () => {
  const rows: PickerRow[] = [
    { kind: 'folder', id: 'f1', name: '原版', sizeBytes: 0 },
    { kind: 'folder', id: 'f2', name: 'IF线 1-2', sizeBytes: 0 },
    { kind: 'file', id: 'x1', name: 'EP1.mp4', sizeBytes: 10 },
    { kind: 'file', id: 'x2', name: 'EP2.mp4', sizeBytes: 20 },
  ];

  it('空关键字原样返回', () => {
    expect(filterPickerRows(rows, '')).toHaveLength(4);
    expect(filterPickerRows(rows, '   ')).toHaveLength(4);
  });

  it('按名字包含过滤，文件夹和文件都能匹配到', () => {
    expect(filterPickerRows(rows, 'ep1').map((row) => row.name)).toEqual(['EP1.mp4']);
    expect(filterPickerRows(rows, '线').map((row) => row.name)).toEqual(['IF线 1-2']);
  });

  it('大小写不敏感（用户不会在意大小写）', () => {
    expect(filterPickerRows(rows, 'Ep2').map((row) => row.name)).toEqual(['EP2.mp4']);
  });

  it('没匹配时返回空数组，不是全部', () => {
    expect(filterPickerRows(rows, '不存在的东西')).toEqual([]);
  });
});

describe('pickerLocationLabel', () => {
  it('根目录显示「我的文件」', () => {
    expect(pickerLocationLabel([])).toBe('我的文件');
    expect(pickerLocationLabel([folder('root', '', true)])).toBe('我的文件');
  });

  it('多层目录拼成路径', () => {
    expect(pickerLocationLabel([folder('root', '', true), folder('a', '原版')])).toBe(
      '我的文件 / 原版',
    );
  });
});

describe('sentMessage', () => {
  it('发文件夹要说明"对方点开就能看到里面的东西"', () => {
    const text = sentMessage({ kind: 'folder', id: 'f', name: '原版', sizeBytes: 0 }, 'klara');
    expect(text).toContain('文件夹');
    expect(text).toContain('点开');
    expect(text).toContain('klara');
  });

  it('发单个文件时说明去哪儿看', () => {
    const text = sentMessage({ kind: 'file', id: 'x', name: 'EP1.mp4', sizeBytes: 1 }, 'klara');
    expect(text).toContain('EP1.mp4');
    expect(text).toContain('分享给我的');
  });
});
