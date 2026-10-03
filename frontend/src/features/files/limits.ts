/**
 * 「快到期」与「快满」的判定（2026-10-03，用户批准的功能之一：回收站保留期提示 + 用量提醒）。
 *
 * 为什么做成纯函数：这些判定决定"要不要提醒用户"，判断错了要么天天吓人（阈值太松）、
 * 要么数据快没了还不吭声（阈值太严），所以用单测把边界钉死（见 tests/limits.test.ts）。
 */

/** 回收站里还有几天会被永久删除；已经到期（后台还没跑）返回 0，算不出来返回 null */
export function daysLeftInTrash(deletedAt: string, retentionDays: number): number | null {
  if (!deletedAt || !Number.isFinite(retentionDays) || retentionDays <= 0) return null;
  const deleted = new Date(deletedAt).getTime();
  if (Number.isNaN(deleted)) return null;
  const purgeAt = deleted + retentionDays * 24 * 60 * 60 * 1000;
  const left = Math.ceil((purgeAt - Date.now()) / (24 * 60 * 60 * 1000));
  return Math.max(0, left);
}

/** 回收站那行的提示文案：不吓人，但要让人知道"什么时候会没" */
export function trashExpiryLabel(deletedAt: string, retentionDays: number): string {
  const days = daysLeftInTrash(deletedAt, retentionDays);
  if (days === null) return '';
  if (days === 0) return '即将被永久删除';
  if (days <= 3) return `${days} 天后永久删除`;
  return `还剩 ${days} 天`;
}

export type QuotaLevel = 'ok' | 'warn' | 'full';

export interface QuotaStatus {
  level: QuotaLevel;
  /** 已用百分比（0~100 的整数，便于直接显示） */
  percent: number;
  usedBytes: number;
  quotaBytes: number;
}

/** 用量阈值：≥80% 提醒，≥95% 严厉提醒；配额为 0（未设置）时不提醒 */
export const QUOTA_WARN_PERCENT = 80;
export const QUOTA_FULL_PERCENT = 95;

export function quotaStatus(usedBytes: number, quotaBytes: number): QuotaStatus {
  const used = Number.isFinite(usedBytes) && usedBytes > 0 ? usedBytes : 0;
  const quota = Number.isFinite(quotaBytes) && quotaBytes > 0 ? quotaBytes : 0;
  if (quota === 0) {
    return { level: 'ok', percent: 0, usedBytes: used, quotaBytes: 0 };
  }
  const percent = Math.min(100, Math.round((used / quota) * 100));
  const level: QuotaLevel =
    percent >= QUOTA_FULL_PERCENT ? 'full' : percent >= QUOTA_WARN_PERCENT ? 'warn' : 'ok';
  return { level, percent, usedBytes: used, quotaBytes: quota };
}
