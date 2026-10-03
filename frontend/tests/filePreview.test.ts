import { describe, expect, it } from 'vitest';

import { canPreview, previewKind } from '../src/services/filePreview';

/**
 * 预览类型判定（2026-10-03，用户要求"图片要有缩略图/预览"）。
 *
 * 这里钉的是"什么能预览、什么必须走下载"：判断错了要么让用户点开一片空白，
 * 要么把二进制当文本渲染成一堆乱码 —— 两种都是"界面骗人"（AGENTS §14.1）。
 */
describe('previewKind', () => {
  it('图片：按 mime 认，也按扩展名兜底', () => {
    expect(previewKind('image/png', 'a.png')).toBe('image');
    expect(previewKind('image/jpeg', '照片.jpg')).toBe('image');
    // 有些上传的 mime 是 application/octet-stream，只能靠扩展名
    expect(previewKind('application/octet-stream', '截图.PNG')).toBe('image');
  });

  it('PDF 与文本各自成类', () => {
    expect(previewKind('application/pdf', '合同.pdf')).toBe('pdf');
    expect(previewKind('text/plain', 'notes.txt')).toBe('text');
    expect(previewKind('text/markdown', 'readme.md')).toBe('text');
    // 代码/配置类也按文本预览（扩展名兜底）
    expect(previewKind('application/octet-stream', 'app.log')).toBe('text');
    expect(previewKind('', 'config.json')).toBe('text');
  });

  it('其它类型不能预览（老实说"请下载"）', () => {
    expect(previewKind('application/zip', '包.zip')).toBe('none');
    expect(previewKind('application/vnd.ms-excel', '报表.xlsx')).toBe('none');
    expect(previewKind('video/mp4', '视频.mp4')).toBe('none');
    expect(previewKind(undefined, '没有扩展名')).toBe('none');
    expect(canPreview('application/zip', '包.zip')).toBe(false);
  });

  it('大写扩展名与多个点都认得', () => {
    expect(previewKind('', 'REPORT.PDF')).toBe('pdf');
    expect(previewKind('', 'archive.tar.gz')).toBe('none');
    expect(previewKind('', 'my.photo.jpeg')).toBe('image');
  });
});
