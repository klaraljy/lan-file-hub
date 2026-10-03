import { describe, expect, it } from 'vitest';

import {
  attentionTitle,
  isAttention,
  isDismissible,
  isFinished,
  orderTasks,
  uploadErrorMessage,
} from '../src/features/uploads/taskView';
import type { UploadTask } from '../src/types/upload';

/**
 * 「哪个文件失败了」必须一眼可见（2026-10-03 用户实测反馈：
 * "刚刚失败了，我想看看哪个失败了，发现根本就看不了"）。
 *
 * 这里钉两件事：① 失败/暂停的任务排在列表最前面并且有单独提示；
 * ② 「清除已完成」不能把失败记录一起清掉 —— 清掉就再也找不回来了。
 */
function task(overrides: Partial<UploadTask> & { id: string }): UploadTask {
  return {
    folderId: 'folder-1',
    name: 'a.mp4',
    size: 1024,
    chunkSize: 8,
    totalChunks: 128,
    completedChunks: [],
    status: 'uploading',
    percent: 0,
    attempts: 0,
    ...overrides,
  };
}

describe('isAttention', () => {
  it('失败 / 过期 / 暂停 / 要重新选文件都算"要用户处理"', () => {
    expect(isAttention(task({ id: '1', status: 'failed' }))).toBe(true);
    expect(isAttention(task({ id: '2', status: 'expired' }))).toBe(true);
    expect(isAttention(task({ id: '3', status: 'paused' }))).toBe(true);
    expect(isAttention(task({ id: '4', status: 'paused', needsFile: true }))).toBe(true);
  });

  it('在跑 / 排队 / 完成的不算', () => {
    expect(isAttention(task({ id: '1', status: 'uploading' }))).toBe(false);
    expect(isAttention(task({ id: '2', status: 'queued' }))).toBe(false);
    expect(isAttention(task({ id: '3', status: 'completed' }))).toBe(false);
    expect(isAttention(task({ id: '4', status: 'cancelled' }))).toBe(false);
  });
});

describe('orderTasks', () => {
  it('要处理的排最前，完成的沉到最后，同级保持原顺序', () => {
    const tasks = [
      task({ id: 'done-1', status: 'completed' }),
      task({ id: 'run-1', status: 'uploading' }),
      task({ id: 'fail-1', status: 'failed' }),
      task({ id: 'done-2', status: 'completed' }),
      task({ id: 'queue-1', status: 'queued' }),
      task({ id: 'fail-2', status: 'expired' }),
      task({ id: 'run-2', status: 'uploading' }),
    ];

    expect(orderTasks(tasks).map((item) => item.id)).toEqual([
      'fail-1',
      'fail-2',
      'run-1',
      'queue-1',
      'run-2',
      'done-1',
      'done-2',
    ]);
  });

  it('不改动传入的数组', () => {
    const tasks = [task({ id: 'a', status: 'completed' }), task({ id: 'b', status: 'failed' })];
    orderTasks(tasks);
    expect(tasks.map((item) => item.id)).toEqual(['a', 'b']);
  });
});

describe('attentionTitle', () => {
  it('把"没传成功"和"暂停/待继续"分开说清楚', () => {
    expect(attentionTitle([task({ id: '1', status: 'failed' })])).toContain('1 个文件没传成功');
    expect(
      attentionTitle([task({ id: '1', status: 'failed' }), task({ id: '2', status: 'paused' })]),
    ).toContain('另有 1 个');
    expect(attentionTitle([task({ id: '1', status: 'paused' })])).toContain('要你处理');
  });
});

describe('清除已完成', () => {
  it('已完成/已取消可以清，失败与过期必须留着', () => {
    expect(isDismissible('completed')).toBe(true);
    expect(isDismissible('cancelled')).toBe(true);
    expect(isDismissible('failed')).toBe(false);
    expect(isDismissible('expired')).toBe(false);
    // 旧的 isFinished 语义保留（终态判定），但不再用于"清除"
    expect(isFinished('failed')).toBe(true);
  });
});

describe('uploadErrorMessage', () => {
  it('优先后端给的中文原因', () => {
    expect(uploadErrorMessage(new Error('你的并发上传任务已达上限，请先完成或取消其他上传'))).toBe(
      '你的并发上传任务已达上限，请先完成或取消其他上传',
    );
  });

  it('拿不到原因时有兜底，不给用户看 undefined', () => {
    expect(uploadErrorMessage(null)).toBe('上传失败，请稍后重试');
    expect(uploadErrorMessage({})).toBe('上传失败，请稍后重试');
  });
});
