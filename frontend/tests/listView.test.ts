import { describe, expect, it } from 'vitest';

import {
  DEFAULT_SORT,
  SORT_FIELDS,
  SORT_FIELD_LABELS,
  buildBrowseRows,
  filterRows,
  formatBytes,
  nextSortState,
  sortIndicator,
  sortRows,
  typeLabel,
  type SortState,
} from '../src/features/files/listView';
import type { FileMeta } from '../src/types/file';
import type { Folder } from '../src/types/folder';

/**
 * 列表的排序/过滤是纯函数，容易被「看起来对」骗过去，所以这里把
 * 「文件夹永远在前」「同值稳定」「时间解析失败要有兜底」这些规则都钉死。
 */

function makeFile(name: string, size: number, updatedAt: string): FileMeta {
  return {
    id: `file-${name}`,
    owner_id: 'user-1',
    folder_id: 'folder-1',
    current_version_id: 'version-1',
    name,
    mime_type: 'application/octet-stream',
    size_bytes: size,
    content_hash: 'a'.repeat(64),
    status: 'ACTIVE',
    created_at: updatedAt,
    updated_at: updatedAt,
    deleted_at: null,
  };
}

function makeFolder(name: string, updatedAt: string): Folder {
  return {
    id: `folder-${name}`,
    owner_id: 'user-1',
    parent_id: 'root',
    name,
    status: 'ACTIVE',
    is_root: false,
    created_at: updatedAt,
    updated_at: updatedAt,
    deleted_at: null,
  };
}

describe('buildBrowseRows', () => {
  it('文件夹在前、文件在后，并带上稳定 key', () => {
    const rows = buildBrowseRows(
      [makeFolder('文档', '2026-01-02T00:00:00Z')],
      [makeFile('a.txt', 10, '2026-01-01T00:00:00Z')],
    );

    expect(rows).toHaveLength(2);
    expect(rows[0]!.kind).toBe('folder');
    expect(rows[0]!.key).toBe('folder-folder-文档');
    expect(rows[1]!.kind).toBe('file');
    expect(rows[1]!.key).toBe('file-file-a.txt');
  });

  it('把原始对象挂在行上，渲染时不用再查一遍', () => {
    const file = makeFile('b.bin', 42, '2026-01-01T00:00:00Z');
    const rows = buildBrowseRows([], [file]);
    const row = rows[0]!;
    expect(row.kind).toBe('file');
    if (row.kind === 'file') {
      expect(row.file).toBe(file);
      expect(row.size_bytes).toBe(42);
      expect(row.content_hash).toBe(file.content_hash);
    }
  });

  it('两个空列表得到空数组', () => {
    expect(buildBrowseRows([], [])).toEqual([]);
  });
});

describe('sortRows', () => {
  const folders = [makeFolder('b目录', '2026-01-05T00:00:00Z'), makeFolder('a目录', '2026-01-01T00:00:00Z')];
  const files = [
    makeFile('文件10.txt', 100, '2026-01-03T00:00:00Z'),
    makeFile('文件2.txt', 300, '2026-01-02T00:00:00Z'),
    makeFile('alpha.txt', 200, '2026-01-04T00:00:00Z'),
  ];
  const rows = buildBrowseRows(folders, files);

  it('无论怎么排，文件夹都在文件前面', () => {
    const sorts: SortState[] = [
      { field: 'name', order: 'asc' },
      { field: 'name', order: 'desc' },
      { field: 'size', order: 'asc' },
      { field: 'size', order: 'desc' },
      { field: 'updated_at', order: 'asc' },
      { field: 'updated_at', order: 'desc' },
    ];
    for (const sort of sorts) {
      const sorted = sortRows(rows, sort);
      const firstFileIndex = sorted.findIndex((row) => row.kind === 'file');
      const lastFolderIndex = sorted.map((row) => row.kind).lastIndexOf('folder');
      expect(lastFolderIndex).toBeLessThan(firstFileIndex);
    }
  });

  it('按名称升序：纯 ASCII 的名称顺序在任何排序规则下都确定', () => {
    const ascii = buildBrowseRows(
      [],
      [
        makeFile('beta.txt', 1, '2026-01-01T00:00:00Z'),
        makeFile('Alpha.txt', 1, '2026-01-01T00:00:00Z'),
        makeFile('gamma.txt', 1, '2026-01-01T00:00:00Z'),
      ],
    );
    const sorted = sortRows(ascii, { field: 'name', order: 'asc' });
    // 大小写不敏感（sensitivity: 'base'）
    expect(sorted.map((row) => row.name)).toEqual(['Alpha.txt', 'beta.txt', 'gamma.txt']);
  });

  it('按名称升序时数字按数值排序（文件2 在 文件10 之前）', () => {
    // 这条是「中文按拼音、数字按数值」里真正影响用户的那一半：
    // 用 code unit 直接比较会把「文件10」排在「文件2」前面
    const sorted = sortRows(rows, { field: 'name', order: 'asc' });
    const fileNames = sorted.filter((row) => row.kind === 'file').map((row) => row.name);
    expect(fileNames.indexOf('文件2.txt')).toBeLessThan(fileNames.indexOf('文件10.txt'));
    expect(fileNames).toContain('alpha.txt');
    // 中文与拉丁字母的相对顺序由 ICU 的 zh-CN 排序规则决定（中文在前），
    // 不写成固定断言，避免换 Node/ICU 版本后误报
  });

  it('按名称降序是精确的逆序', () => {
    const asc = sortRows(rows, { field: 'name', order: 'asc' }).map((row) => row.key);
    const desc = sortRows(rows, { field: 'name', order: 'desc' }).map((row) => row.key);
    // 文件夹块与文件块内部各自逆序（分组不变）
    expect(desc).toEqual([...asc.slice(0, 2).reverse(), ...asc.slice(2).reverse()]);
  });

  it('按大小排序只影响文件组', () => {
    const sorted = sortRows(rows, { field: 'size', order: 'asc' });
    const fileNames = sorted.filter((row) => row.kind === 'file').map((row) => row.name);
    expect(fileNames).toEqual(['文件10.txt', 'alpha.txt', '文件2.txt']);

    const desc = sortRows(rows, { field: 'size', order: 'desc' });
    expect(desc.filter((row) => row.kind === 'file').map((row) => row.name)).toEqual([
      '文件2.txt',
      'alpha.txt',
      '文件10.txt',
    ]);
  });

  it('按修改时间排序', () => {
    const asc = sortRows(rows, { field: 'updated_at', order: 'asc' });
    expect(asc.filter((row) => row.kind === 'file').map((row) => row.name)).toEqual([
      '文件2.txt',
      '文件10.txt',
      'alpha.txt',
    ]);
  });

  it('时间无法解析时退化成按名称比较，不会输出随机顺序', () => {
    const broken = buildBrowseRows(
      [],
      [makeFile('b.txt', 1, '不是时间'), makeFile('a.txt', 1, '也不是时间')],
    );
    const sorted = sortRows(broken, { field: 'updated_at', order: 'asc' });
    expect(sorted.map((row) => row.name)).toEqual(['a.txt', 'b.txt']);
  });

  it('不改动传入的数组（纯函数）', () => {
    const copy = [...rows];
    sortRows(rows, { field: 'size', order: 'desc' });
    expect(rows).toEqual(copy);
  });
});

describe('filterRows', () => {
  const rows = buildBrowseRows(
    [makeFolder('Project', '2026-01-01T00:00:00Z')],
    [makeFile('report.PDF', 1, '2026-01-01T00:00:00Z'), makeFile('照片.png', 1, '2026-01-01T00:00:00Z')],
  );

  it('关键字为空时原样返回', () => {
    expect(filterRows(rows, '')).toHaveLength(3);
    expect(filterRows(rows, '   ')).toHaveLength(3);
  });

  it('不区分大小写', () => {
    expect(filterRows(rows, 'pdf').map((row) => row.name)).toEqual(['report.PDF']);
    expect(filterRows(rows, 'REPORT').map((row) => row.name)).toEqual(['report.PDF']);
  });

  it('支持子串匹配与中文', () => {
    expect(filterRows(rows, '照片').map((row) => row.name)).toEqual(['照片.png']);
    expect(filterRows(rows, 'oje').map((row) => row.name)).toEqual(['Project']);
  });

  it('首尾空格会被忽略', () => {
    expect(filterRows(rows, '  png  ').map((row) => row.name)).toEqual(['照片.png']);
  });

  it('没有匹配时返回空数组', () => {
    expect(filterRows(rows, '不存在的名字')).toEqual([]);
  });
});

describe('nextSortState', () => {
  it('点同一字段在升降序之间切换', () => {
    expect(nextSortState({ field: 'name', order: 'asc' }, 'name')).toEqual({
      field: 'name',
      order: 'desc',
    });
    expect(nextSortState({ field: 'name', order: 'desc' }, 'name')).toEqual({
      field: 'name',
      order: 'asc',
    });
  });

  it('换字段时给出符合直觉的默认方向', () => {
    // 名称默认升序（A→Z 好找）；大小与时间默认降序（先看大的/新的）
    expect(nextSortState(DEFAULT_SORT, 'size')).toEqual({ field: 'size', order: 'desc' });
    expect(nextSortState(DEFAULT_SORT, 'updated_at')).toEqual({
      field: 'updated_at',
      order: 'desc',
    });
    expect(nextSortState({ field: 'size', order: 'desc' }, 'name')).toEqual({
      field: 'name',
      order: 'asc',
    });
  });

  it('排序指示只出现在当前字段上', () => {
    const sort: SortState = { field: 'size', order: 'desc' };
    expect(sortIndicator(sort, 'size')).toBe(' ↓');
    expect(sortIndicator(sort, 'name')).toBe('');
    expect(sortIndicator({ field: 'name', order: 'asc' }, 'name')).toBe(' ↑');
  });

  it('排序菜单只有三个字段，且都有标签', () => {
    expect([...SORT_FIELDS]).toEqual(['name', 'size', 'updated_at']);
    for (const field of SORT_FIELDS) {
      expect(SORT_FIELD_LABELS[field]).not.toBe('');
    }
  });
});

describe('展示辅助', () => {
  it('字节数格式化', () => {
    expect(formatBytes(0)).toBe('0 B');
    expect(formatBytes(512)).toBe('512 B');
    expect(formatBytes(2048)).toBe('2.0 KB');
    expect(formatBytes(5 * 1024 * 1024)).toBe('5.0 MB');
    expect(formatBytes(3 * 1024 * 1024 * 1024)).toBe('3.00 GB');
    // 非法输入不能显示 NaN
    expect(formatBytes(Number.NaN)).toBe('0 B');
  });

  it('类型标签优先用 MIME，缺失时按扩展名兜底', () => {
    expect(typeLabel(makeFile('a.txt', 1, '2026-01-01T00:00:00Z'))).toBe(
      'application/octet-stream',
    );
    const noMime = { ...makeFile('b.tar.gz', 1, '2026-01-01T00:00:00Z'), mime_type: '' };
    expect(typeLabel(noMime)).toBe('GZ');
    const noExtension = { ...makeFile('README', 1, '2026-01-01T00:00:00Z'), mime_type: '' };
    expect(typeLabel(noExtension)).toBe('未知类型');
  });
});
