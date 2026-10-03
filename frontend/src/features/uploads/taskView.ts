/**
 * 上传任务列表的展示逻辑（纯函数，2026-10-03）。
 *
 * 来历：用户实测反馈「刚刚失败了，我想看看哪个失败了，发现根本就看不了」——
 * 一批几十个文件时，失败的那条混在列表中间、只有一个小红标，等于看不见。
 * 这里把「哪些要用户处理、怎么排序、提示语怎么写」抽出来，UI 与单测共用。
 */
import type { UploadTask, UploadTaskStatus } from '../../types/upload';

/** 正在跑（含在算哈希与重试）的状态 */
export const RUNNING_STATUSES: UploadTaskStatus[] = ['uploading', 'hashing', 'retrying'];

/**
 * 需要用户看一眼的状态：失败 / 会话过期 / 要重新选文件 / 已暂停。
 * 「哪个文件没传成功」属于这一类，必须排在最前面。
 */
export function isAttention(task: UploadTask): boolean {
  return (
    task.status === 'failed' ||
    task.status === 'expired' ||
    task.status === 'paused' ||
    task.needsFile === true
  );
}

/** 排序：要处理的排最前 → 在跑的 → 排队的 → 完成的；同级保持原有顺序 */
export function orderTasks(tasks: UploadTask[]): UploadTask[] {
  const rank = (task: UploadTask): number => {
    if (isAttention(task)) return 0;
    if (RUNNING_STATUSES.includes(task.status) || task.status === 'queued') return 1;
    if (task.status === 'completed') return 3;
    return 2;
  };
  return tasks
    .map((task, index) => ({ task, index }))
    .sort((a, b) => rank(a.task) - rank(b.task) || a.index - b.index)
    .map((item) => item.task);
}

/** 提示语：把「没传成功」和「暂停/待继续」分开说，别让用户以为暂停也是失败 */
export function attentionTitle(tasks: UploadTask[]): string {
  const failed = tasks.filter(
    (task) => task.status === 'failed' || task.status === 'expired',
  ).length;
  const rest = tasks.length - failed;
  if (failed > 0 && rest > 0) return `${failed} 个文件没传成功，另有 ${rest} 个要你处理`;
  if (failed > 0) return `${failed} 个文件没传成功，看这里就够了`;
  return `有 ${rest} 个上传任务要你处理`;
}

/** 终态（不会再自己变化） */
export function isFinished(status: UploadTaskStatus): boolean {
  return status === 'completed' || status === 'failed' || status === 'cancelled';
}

/**
 * 「清除已完成」能清掉的状态。
 * ⛔ 失败 / 过期**不在其中**：清掉之后用户再也看不到是哪个文件没传成功。
 */
export function isDismissible(status: UploadTaskStatus): boolean {
  return status === 'completed' || status === 'cancelled';
}

/** 失败原因：优先用后端给的那句话（本来就是中文、且可操作） */
export function uploadErrorMessage(error: unknown): string {
  if (error && typeof error === 'object' && 'message' in error) {
    const message = String((error as { message?: unknown }).message ?? '');
    if (message) return message;
  }
  return '上传失败，请稍后重试';
}
