import { describe, expect, it } from 'vitest';

import {
  canPlayInBrowser,
  canPreview,
  previewHint,
  previewKind,
} from '../src/services/filePreview';

/**
 * 预览类型判定（2026-10-03，用户要求"图片要有缩略图/预览"，当天补齐视频/音频/Office）。
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
    // CSV 是纯文本，按文本看（不是 Excel）
    expect(previewKind('', '报表.csv')).toBe('text');
  });

  it('视频与音频单独成类（2026-10-03 用户要求能看视频）', () => {
    expect(previewKind('video/mp4', 'EP1.mp4')).toBe('video');
    expect(previewKind('application/octet-stream', '手机拍的.MP4')).toBe('video');
    expect(previewKind('video/quicktime', '片子.mov')).toBe('video');
    expect(previewKind('audio/mpeg', '歌.mp3')).toBe('audio');
    expect(previewKind('', '录音.m4a')).toBe('audio');
  });

  it('Word / Excel / PPT 认成 office（网页里不渲染，但要说清怎么打开）', () => {
    expect(previewKind('application/vnd.ms-excel', '报表.xlsx')).toBe('office');
    expect(
      previewKind(
        'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
        '报表.xlsx',
      ),
    ).toBe('office');
    expect(previewKind('application/msword', '合同.doc')).toBe('office');
    expect(previewKind('', '演示.pptx')).toBe('office');
    expect(previewKind('', '表格.et')).toBe('office');
  });

  it('其它类型不能预览（老实说"请下载"）', () => {
    expect(previewKind('application/zip', '包.zip')).toBe('none');
    expect(previewKind('application/x-msdownload', 'setup.exe')).toBe('none');
    expect(previewKind(undefined, '没有扩展名')).toBe('none');
    expect(canPreview('application/zip', '包.zip')).toBe(false);
  });

  it('大写扩展名与多个点都认得', () => {
    expect(previewKind('', 'REPORT.PDF')).toBe('pdf');
    expect(previewKind('', 'archive.tar.gz')).toBe('none');
    expect(previewKind('', 'my.photo.jpeg')).toBe('image');
  });
});

describe('canPlayInBrowser', () => {
  it('浏览器能直接播的容器才给播放器', () => {
    expect(canPlayInBrowser('video/mp4', 'EP1.mp4')).toBe(true);
    expect(canPlayInBrowser('video/webm', 'a.webm')).toBe(true);
    expect(canPlayInBrowser('audio/mpeg', '歌.mp3')).toBe(true);
    // 没有扩展名时看 mime
    expect(canPlayInBrowser('video/mp4', 'no-extension')).toBe(true);
  });

  it('浏览器播不了的容器不要摆播放器（用户会看到一片黑）', () => {
    expect(canPlayInBrowser('video/x-matroska', '电影.mkv')).toBe(false);
    expect(canPlayInBrowser('video/x-msvideo', '老片.avi')).toBe(false);
    expect(canPlayInBrowser('video/quicktime', '手机拍的.mov')).toBe(false);
    // 不是视频/音频的一律 false
    expect(canPlayInBrowser('image/png', 'a.png')).toBe(false);
  });
});

describe('previewHint', () => {
  it('看不了的时候要给出下一步怎么做，而不是只说"不支持"', () => {
    expect(previewHint('office', '报表.xlsx')).toContain('下载');
    expect(previewHint('office', '报表.xlsx')).toContain('WPS');
    expect(previewHint('video', '电影.mkv')).toContain('下载');
    expect(previewHint('none', '包.zip')).toContain('下载');
  });
});
