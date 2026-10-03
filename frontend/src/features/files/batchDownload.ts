/**
 * 批量下载（2026-10-03 用户要求："我是想一同下载文件夹里面的东西，而不是打包成 zip"）。
 *
 * 做法：**逐个走既有的"一次性票据 + 浏览器原生下载"**，不打包、不占额外磁盘、
 * 一个失败不影响其它；手机端与电脑端用的是同一套（同一个函数）。
 *
 * 浏览器限制（必须让用户知道，不然会以为坏了）：
 *   - 一次点出很多个下载时，Chrome/Edge 会弹「是否允许下载多个文件？」—— 点允许才继续；
 *   - 所以我们**串行**发（前一个开始下一个），间隔一点点，并给出「正在下载 3/12」的提示。
 */

/** 两个下载之间隔多久（毫秒）：太快会被浏览器当成弹窗滥用，太慢用户等得烦 */
export const BATCH_DOWNLOAD_GAP_MS = 250;

export interface BatchDownloadItem {
  id: string;
  name: string;
  sizeBytes?: number;
}

/** 给用户看的进度文案（纯函数，单测覆盖） */
export function batchProgressText(done: number, total: number): string {
  return `正在下载 ${done}/${total}…（浏览器可能问一次「是否允许下载多个文件」，点允许即可）`;
}

export function batchDoneText(done: number, failed: number): string {
  if (failed === 0) {
    return `已开始下载 ${done} 个文件`;
  }
  return `已开始下载 ${done - failed} 个文件，${failed} 个失败（可重试）`;
}

/** 多个文件时提醒一句"会下很多个"，避免用户以为只下了一个 */
export function batchStartText(total: number): string {
  return total > 1 ? `要下载 ${total} 个文件` : '开始下载';
}

/**
 * 串行下载一批文件。
 *
 * `downloadOne` 由调用方注入（真实实现是"取票据 → 触发浏览器下载"），
 * 这里只负责顺序、间隔与进度回调 —— 因此可以单测，不必真的开浏览器。
 */
export async function downloadSequentially(
  items: BatchDownloadItem[],
  downloadOne: (item: BatchDownloadItem) => Promise<void>,
  hooks: {
    onProgress?: (done: number, total: number) => void;
    /** 两个下载之间的等待，默认 250ms；测试里传 0 */
    gapMs?: number;
    sleep?: (ms: number) => Promise<void>;
  } = {},
): Promise<{ done: number; failed: number; failedItems: BatchDownloadItem[] }> {
  const sleep = hooks.sleep ?? ((ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms)));
  const gap = hooks.gapMs ?? BATCH_DOWNLOAD_GAP_MS;
  let done = 0;
  let failed = 0;
  const failedItems: BatchDownloadItem[] = [];

  for (const item of items) {
    try {
      await downloadOne(item);
    } catch {
      failed += 1;
      failedItems.push(item);
    }
    done += 1;
    hooks.onProgress?.(done, items.length);
    if (gap > 0 && done < items.length) {
      await sleep(gap);
    }
  }
  return { done, failed, failedItems };
}
