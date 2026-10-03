import { describe, expect, it, vi, afterEach } from 'vitest';

import { daysLeftInTrash, quotaStatus, trashExpiryLabel } from '../src/features/files/limits';

/**
 * 「还剩几天被删」与「空间快满了」（2026-10-03，用户批准的功能）。
 *
 * 这些判定决定"要不要打扰用户"，所以边界必须钉住：
 *   - 保留期提示不能算错（说错天数 ＝ 用户以为还有时间，结果文件没了）；
 *   - 用量提醒不能误报（天天弹横幅 ＝ 用户直接无视它）。
 */
afterEach(() => {
  vi.useRealTimers();
});

describe('daysLeftInTrash', () => {
  it('按删除时间 + 保留天数算剩余天数', () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-10-03T12:00:00Z'));

    // 删了 1 天，保留 30 天 → 还剩 29 天
    expect(daysLeftInTrash('2026-10-02T12:00:00Z', 30)).toBe(29);
    // 刚删 → 30 天
    expect(daysLeftInTrash('2026-10-03T12:00:00Z', 30)).toBe(30);
    // 已经到期（后台还没跑）→ 0，不出现负数
    expect(daysLeftInTrash('2026-08-01T12:00:00Z', 30)).toBe(0);
  });

  it('数据不全时返回 null（界面就不显示这一列，而不是显示 NaN）', () => {
    expect(daysLeftInTrash('', 30)).toBeNull();
    expect(daysLeftInTrash('不是时间', 30)).toBeNull();
    expect(daysLeftInTrash('2026-10-02T12:00:00Z', 0)).toBeNull();
    expect(daysLeftInTrash('2026-10-02T12:00:00Z', Number.NaN)).toBeNull();
  });
});

describe('trashExpiryLabel', () => {
  it('按剩余天数说人话（越接近越严肃）', () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-10-03T12:00:00Z'));

    expect(trashExpiryLabel('2026-10-03T12:00:00Z', 30)).toBe('还剩 30 天');
    expect(trashExpiryLabel('2026-10-01T12:00:00Z', 30)).toBe('还剩 28 天');
    expect(trashExpiryLabel('2026-09-05T12:00:00Z', 30)).toBe('2 天后永久删除');
    expect(trashExpiryLabel('2026-08-01T12:00:00Z', 30)).toBe('即将被永久删除');
    expect(trashExpiryLabel('', 30)).toBe('');
  });
});

describe('quotaStatus', () => {
  const GB = 1024 * 1024 * 1024;

  it('低于 80% 不打扰用户', () => {
    expect(quotaStatus(0, 20 * GB).level).toBe('ok');
    expect(quotaStatus(10 * GB, 20 * GB).level).toBe('ok');
    expect(quotaStatus(15 * GB, 20 * GB).level).toBe('ok'); // 75%
  });

  it('80% 起温和提醒，95% 起严厉提醒', () => {
    expect(quotaStatus(16 * GB, 20 * GB).level).toBe('warn'); // 80%
    expect(quotaStatus(16 * GB, 20 * GB).percent).toBe(80);
    expect(quotaStatus(19 * GB, 20 * GB).level).toBe('full'); // 95%
    // 用满也不会显示超过 100%
    expect(quotaStatus(25 * GB, 20 * GB).percent).toBe(100);
    expect(quotaStatus(25 * GB, 20 * GB).level).toBe('full');
  });

  it('没设配额（0）或数据缺失时不提醒', () => {
    expect(quotaStatus(5 * GB, 0).level).toBe('ok');
    expect(quotaStatus(Number.NaN, 20 * GB).level).toBe('ok');
    expect(quotaStatus(-1, 20 * GB).percent).toBe(0);
  });
});
