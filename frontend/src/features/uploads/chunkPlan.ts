/**
 * 分块上传的纯逻辑：分块规划、断点续传筛选、并发控制与重试策略。
 *
 * 这里不碰网络、不碰 DOM，全部可单测；实际请求在 uploadManager.ts 里。
 * 规则来源：docs/frontend.md §5、docs/storage.md §分块协议。
 */

/** 同时在传的分块数量上限（docs/frontend.md §5.2） */
export const MAX_PARALLEL_CHUNKS = 4;

/** 自动重试的退避时间（docs/frontend.md §5.6） */
export const RETRY_DELAYS_MS = [500, 1000, 2000] as const;

/** 可重试的 HTTP 状态码（docs/frontend.md §5.4） */
const RETRYABLE_STATUS = new Set([408, 429, 502, 503, 504]);

/** 可重试的错误码：网络问题与服务端暂时不可用 */
const RETRYABLE_CODES = new Set(['NETWORK_ERROR', 'RATE_LIMITED', 'STORAGE_UNAVAILABLE']);

export interface ChunkSlice {
  index: number;
  /** 起始字节（含） */
  start: number;
  /** 结束字节（不含） */
  end: number;
}

/**
 * 把文件切成上传分块。
 * 0 字节文件也返回一个空分块：后端同样按 1 块处理（ComputeTotalChunks）。
 */
export function planChunks(size: number, chunkSize: number): ChunkSlice[] {
  if (!Number.isFinite(size) || size < 0) {
    throw new Error('文件大小不合法');
  }
  if (!Number.isFinite(chunkSize) || chunkSize <= 0) {
    throw new Error('分块大小不合法');
  }
  const chunks: ChunkSlice[] = [];
  if (size === 0) {
    chunks.push({ index: 0, start: 0, end: 0 });
    return chunks;
  }
  for (let start = 0, index = 0; start < size; start += chunkSize, index += 1) {
    chunks.push({ index, start, end: Math.min(start + chunkSize, size) });
  }
  return chunks;
}

/** 后端已收到的分块要跳过；前端不假设分块按顺序到达 */
export function missingChunks(plan: ChunkSlice[], completedChunks: number[]): ChunkSlice[] {
  const done = new Set(completedChunks);
  return plan.filter((chunk) => !done.has(chunk.index));
}

export interface RetryableLike {
  code?: string;
  status?: number;
}

/** 是否值得自动重试：网络断开、超时、限流与服务端暂时不可用 */
export function isRetryableError(error: RetryableLike | null | undefined): boolean {
  if (!error) return false;
  if (error.status !== undefined && RETRYABLE_STATUS.has(error.status)) {
    return true;
  }
  return error.code !== undefined && RETRYABLE_CODES.has(error.code);
}

/**
 * 第 attempt 次重试前的等待时间（attempt 从 1 开始）。
 * 超过退避表长度返回 null，表示不再自动重试。
 */
export function retryDelayMs(attempt: number, random: () => number = Math.random): number | null {
  if (attempt < 1 || attempt > RETRY_DELAYS_MS.length) {
    return null;
  }
  const base = RETRY_DELAYS_MS[attempt - 1]!;
  // 加 0~20% 抖动，避免大量请求同时重试
  return Math.round(base * (1 + random() * 0.2));
}

/**
 * 以固定并发上限跑一批任务。
 * 每个任务内部自己处理重试，这里只保证并发度；
 * 某个任务失败不会中断其他任务，结果按输入顺序返回。
 */
export async function runWithConcurrency<T, R>(
  items: T[],
  limit: number,
  worker: (item: T, index: number) => Promise<R>,
): Promise<Array<{ ok: true; value: R } | { ok: false; error: unknown }>> {
  const results = new Array<{ ok: true; value: R } | { ok: false; error: unknown }>(items.length);
  const effectiveLimit = Math.max(1, Math.min(limit, items.length || 1));
  let next = 0;

  async function run(): Promise<void> {
    for (;;) {
      const index = next;
      next += 1;
      if (index >= items.length) {
        return;
      }
      try {
        results[index] = { ok: true, value: await worker(items[index]!, index) };
      } catch (error) {
        results[index] = { ok: false, error };
      }
    }
  }

  await Promise.all(Array.from({ length: effectiveLimit }, () => run()));
  return results;
}

/** 人类可读的速率，用于上传面板提示 */
export function formatRate(bytesPerSecond: number): string {
  if (!Number.isFinite(bytesPerSecond) || bytesPerSecond <= 0) return '—';
  if (bytesPerSecond < 1024) return `${bytesPerSecond.toFixed(0)} B/s`;
  if (bytesPerSecond < 1024 * 1024) return `${(bytesPerSecond / 1024).toFixed(0)} KB/s`;
  return `${(bytesPerSecond / 1024 / 1024).toFixed(1)} MB/s`;
}
