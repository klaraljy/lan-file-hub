import { describe, expect, it } from 'vitest';

import {
  MAX_FOLDER_FILES,
  planFolderUpload,
  sanitizeSegment,
  splitRelativePath,
  summarizePlan,
} from '../src/features/uploads/folderUpload';

/**
 * 「上传文件夹」（2026-10-03 用户要求：整个目录传上去、保留结构）。
 *
 * 这里钉的是**路径处理与防呆**——传错目录结构比传失败更糟（用户会在一堆乱目录里找文件）：
 *   - 每段目录名都要净化，`.` / `..` / 反斜杠 / 控制字符一律不许留在路径里；
 *   - 中间某段不合法 → 整个文件跳过，**不许静默改到别的目录**；
 *   - 目录要按"浅 → 深"给出，父目录必须先建；
 *   - 有数量与深度上限（防误拖整个 C:\）。
 */
function makeFile(name: string, relativePath?: string, size = 10): File {
  const file = new File(['x'.repeat(size)], name, { type: 'application/octet-stream' });
  if (relativePath !== undefined) {
    Object.defineProperty(file, 'webkitRelativePath', { value: relativePath, configurable: true });
  }
  return file;
}

describe('sanitizeSegment', () => {
  it('挡掉路径分隔符、控制字符与 Windows 非法字符', () => {
    expect(sanitizeSegment('扫描件')).toBe('扫描件');
    expect(sanitizeSegment('a/b')).toBe('a_b');
    expect(sanitizeSegment('a\\b')).toBe('a_b');
    expect(sanitizeSegment('a:b*c?')).toBe('a_b_c_');
    expect(sanitizeSegment('a\u0000b')).toBe('a_b');
  });

  it('拒绝空、点、双点（这些会让路径跑到别处）', () => {
    expect(sanitizeSegment('')).toBe('');
    expect(sanitizeSegment('   ')).toBe('');
    expect(sanitizeSegment('.')).toBe('');
    expect(sanitizeSegment('..')).toBe('');
  });
});

describe('splitRelativePath', () => {
  it('普通拖文件：目录为空、名字就是文件名', () => {
    expect(splitRelativePath('', '报表.pdf')).toEqual({ segments: [], name: '报表.pdf' });
  });

  it('目录选择器给的相对路径：切成目录段 + 文件名', () => {
    expect(splitRelativePath('扫描件/2026/报表.pdf', '报表.pdf')).toEqual({
      segments: ['扫描件', '2026'],
      name: '报表.pdf',
    });
  });

  it('反斜杠也认（有些系统/来源会给 Windows 分隔符）', () => {
    expect(splitRelativePath('a\\b\\c.txt', 'c.txt')).toEqual({
      segments: ['a', 'b'],
      name: 'c.txt',
    });
  });

  it('路径里有 .. 时整个文件跳过（不许悄悄改到别的目录）', () => {
    expect(splitRelativePath('../../etc/passwd', 'passwd')).toBeNull();
    expect(splitRelativePath('a/../b.txt', 'b.txt')).toBeNull();
  });

  it('文件名为空或全是非法字符时跳过', () => {
    expect(splitRelativePath('', '')).toBeNull();
    expect(splitRelativePath('a/', '')).toBeNull();
  });
});

describe('planFolderUpload', () => {
  it('建出每一级目录，并按浅 → 深排序', () => {
    const plan = planFolderUpload([
      makeFile('报表.pdf', '扫描件/2026/报表.pdf', 100),
      makeFile('说明.txt', '扫描件/说明.txt', 20),
    ]);

    expect(plan.files).toHaveLength(2);
    expect(plan.directories).toEqual(['扫描件', '扫描件/2026']);
    expect(plan.skipped).toBe(0);
    expect(plan.files[0]!.dirPath).toBe('扫描件/2026');
    expect(plan.files[0]!.name).toBe('报表.pdf');
  });

  it('根目录下的文件不需要建目录', () => {
    const plan = planFolderUpload([makeFile('根.txt', '根.txt')]);
    expect(plan.directories).toEqual([]);
    expect(plan.files[0]!.dirPath).toBe('');
  });

  it('不合法的路径计入 skipped，不影响别的文件', () => {
    const plan = planFolderUpload([makeFile('ok.txt', 'a/ok.txt'), makeFile('passwd', '../passwd')]);
    expect(plan.files).toHaveLength(1);
    expect(plan.skipped).toBe(1);
  });

  it('超过文件数量上限时截断并计入 skipped（防误拖整个盘）', () => {
    const many: File[] = [];
    for (let index = 0; index < MAX_FOLDER_FILES + 5; index++) {
      many.push(makeFile(`f${index}.txt`, `批量/f${index}.txt`));
    }
    const plan = planFolderUpload(many);
    expect(plan.files.length).toBe(MAX_FOLDER_FILES);
    expect(plan.skipped).toBeGreaterThan(0);
  });
});

describe('summarizePlan', () => {
  it('给出文件数、目录数与总字节数（确认框要用）', () => {
    const plan = planFolderUpload([
      makeFile('a.txt', 'x/a.txt', 100),
      makeFile('b.txt', 'x/y/b.txt', 200),
    ]);
    expect(summarizePlan(plan)).toEqual({ files: 2, directories: 2, bytes: 300 });
  });
});
