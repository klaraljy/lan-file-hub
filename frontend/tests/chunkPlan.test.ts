import { describe, expect, it, vi } from 'vitest';

import {
  MAX_PARALLEL_CHUNKS,
  RETRY_DELAYS_MS,
  SLOT_WAIT_DELAYS_MS,
  isRetryableError,
  isStorageBusyError,
  missingChunks,
  planChunks,
  retryDelayMs,
  runWithConcurrency,
  slotWaitDelayMs,
} from '../src/features/uploads/chunkPlan';

describe('planChunks', () => {
  it('按分块大小切分，最后一块可能不满', () => {
    const chunks = planChunks(20, 8);
    expect(chunks).toEqual([
      { index: 0, start: 0, end: 8 },
      { index: 1, start: 8, end: 16 },
      { index: 2, start: 16, end: 20 },
    ]);
  });

  it('整除时不会多出一个空块', () => {
    expect(planChunks(16, 8).map((chunk) => chunk.index)).toEqual([0, 1]);
  });

  it('0 字节文件也算一块（与后端 ComputeTotalChunks 一致）', () => {
    expect(planChunks(0, 8)).toEqual([{ index: 0, start: 0, end: 0 }]);
  });

  it('分块索引从 0 开始且连续', () => {
    const chunks = planChunks(1000, 256);
    expect(chunks.map((chunk) => chunk.index)).toEqual([0, 1, 2, 3]);
    expect(chunks[3]).toEqual({ index: 3, start: 768, end: 1000 });
  });

  it('非法参数必须报错，而不是静默切错', () => {
    expect(() => planChunks(-1, 8)).toThrow();
    expect(() => planChunks(10, 0)).toThrow();
    expect(() => planChunks(Number.NaN, 8)).toThrow();
  });
});

describe('missingChunks', () => {
  const plan = planChunks(20, 8);

  it('过滤掉后端已完成的分块', () => {
    expect(missingChunks(plan, [0, 2]).map((chunk) => chunk.index)).toEqual([1]);
  });

  it('不依赖分块顺序到达', () => {
    expect(missingChunks(plan, [2, 0, 1]).map((chunk) => chunk.index)).toEqual([]);
  });

  it('全部缺失时返回全部分块', () => {
    expect(missingChunks(plan, []).length).toBe(3);
  });

  it('忽略越界的已完成索引', () => {
    expect(missingChunks(plan, [0, 1, 2, 99]).length).toBe(0);
  });
});

describe('重试策略', () => {
  it('网络与服务端暂时不可用可重试', () => {
    expect(isRetryableError({ code: 'NETWORK_ERROR' })).toBe(true);
    expect(isRetryableError({ code: 'STORAGE_UNAVAILABLE' })).toBe(true);
    expect(isRetryableError({ code: 'RATE_LIMITED' })).toBe(true);
    // 并发名额已满（2026-10-03）：等一会儿就能传，不该直接判失败
    expect(isRetryableError({ code: 'STORAGE_BUSY' })).toBe(true);
    for (const status of [408, 429, 502, 503, 504]) {
      expect(isRetryableError({ status })).toBe(true);
    }
  });

  it('客户端错误与冲突不可重试（docs/frontend.md §5.5）', () => {
    for (const status of [400, 401, 403, 404, 409, 413, 422]) {
      expect(isRetryableError({ status })).toBe(false);
    }
    for (const code of ['CHUNK_CONFLICT', 'HASH_MISMATCH', 'QUOTA_EXCEEDED', 'UPLOAD_EXPIRED']) {
      expect(isRetryableError({ code })).toBe(false);
    }
    expect(isRetryableError(null)).toBe(false);
    expect(isRetryableError(undefined)).toBe(false);
  });

  it('退避时间按 500ms / 1s / 2s 递增并带抖动', () => {
    expect(retryDelayMs(1, () => 0)).toBe(RETRY_DELAYS_MS[0]);
    expect(retryDelayMs(2, () => 0)).toBe(RETRY_DELAYS_MS[1]);
    expect(retryDelayMs(3, () => 0)).toBe(RETRY_DELAYS_MS[2]);
    // 抖动上限 20%
    expect(retryDelayMs(3, () => 1)).toBe(Math.round(RETRY_DELAYS_MS[2] * 1.2));
  });

  it('超过次数上限后不再自动重试', () => {
    expect(retryDelayMs(RETRY_DELAYS_MS.length + 1, () => 0)).toBeNull();
    expect(retryDelayMs(0, () => 0)).toBeNull();
  });

  it('等名额的退避比普通重试更长，且超限后不再等', () => {
    expect(slotWaitDelayMs(1, () => 0)).toBe(SLOT_WAIT_DELAYS_MS[0]);
    expect(slotWaitDelayMs(4, () => 0)).toBe(SLOT_WAIT_DELAYS_MS[3]);
    expect(slotWaitDelayMs(4, () => 0)!).toBeGreaterThan(RETRY_DELAYS_MS[2]!);
    expect(slotWaitDelayMs(SLOT_WAIT_DELAYS_MS.length + 1, () => 0)).toBeNull();
    expect(slotWaitDelayMs(0, () => 0)).toBeNull();
  });

  it('只有 STORAGE_BUSY 才算「名额已满」', () => {
    expect(isStorageBusyError({ code: 'STORAGE_BUSY' })).toBe(true);
    expect(isStorageBusyError({ code: 'STORAGE_UNAVAILABLE' })).toBe(false);
    expect(isStorageBusyError({ code: 'QUOTA_EXCEEDED' })).toBe(false);
    expect(isStorageBusyError(null)).toBe(false);
  });
});

describe('runWithConcurrency', () => {
  it('并发度不超过上限', async () => {
    let running = 0;
    let peak = 0;
    const items = Array.from({ length: 10 }, (_, index) => index);

    const results = await runWithConcurrency(items, 3, async (item) => {
      running += 1;
      peak = Math.max(peak, running);
      await new Promise((resolve) => setTimeout(resolve, 5));
      running -= 1;
      return item * 2;
    });

    expect(peak).toBeLessThanOrEqual(3);
    expect(results.every((result) => result.ok)).toBe(true);
    expect(results.map((result) => (result.ok ? result.value : null))).toEqual(
      items.map((item) => item * 2),
    );
  });

  it('单个任务失败不影响其他任务，失败位置保留错误', async () => {
    const results = await runWithConcurrency([1, 2, 3, 4], 2, async (item) => {
      if (item === 3) throw new Error('boom');
      return item;
    });

    expect(results[0]).toEqual({ ok: true, value: 1 });
    expect(results[2]!.ok).toBe(false);
    expect(results[3]).toEqual({ ok: true, value: 4 });
  });

  it('空列表不报错也不调用 worker', async () => {
    const worker = vi.fn();
    const results = await runWithConcurrency([], MAX_PARALLEL_CHUNKS, worker);
    expect(results).toEqual([]);
    expect(worker).not.toHaveBeenCalled();
  });
});
