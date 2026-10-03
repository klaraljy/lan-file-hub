import { describe, expect, it } from 'vitest';

import { resolveUploadTarget } from '../src/features/uploads/uploadTarget';

/**
 * 上传页的目标文件夹（2026-10-03 用户要求"上传和我的文件分成两个入口"）。
 *
 * 规则很简单但错了很难受：传错文件夹，用户会以为文件丢了。
 *   - 从「我的文件」点「上传到这里」过来时 URL 带 `?folder=<id>` → 用 URL 的；
 *   - 直接点左侧「上传文件」进来 → 落在根目录。
 */
describe('resolveUploadTarget', () => {
  it('URL 里带了目标目录就用它', () => {
    expect(resolveUploadTarget('folder-abc', 'root-id')).toBe('folder-abc');
  });

  it('没带就落在根目录', () => {
    expect(resolveUploadTarget(null, 'root-id')).toBe('root-id');
  });

  it('根目录还没读出来时先返回 undefined（上传区据此置灰，不能瞎传）', () => {
    expect(resolveUploadTarget(null, undefined)).toBeUndefined();
  });

  it('空字符串按"没带"处理', () => {
    // URL 上写了 ?folder= 但值为空时，AntD/浏览器可能给空串
    expect(resolveUploadTarget('', 'root-id')).toBe('root-id');
  });
});
