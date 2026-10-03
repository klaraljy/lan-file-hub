/**
 * 上传页的目标文件夹解析（纯函数，2026-10-03 用户要求"上传和我的文件分成两个入口"）。
 *
 * 规则很小但错了很难受：传错文件夹，用户会以为文件丢了。
 *   - 从「我的文件」点「上传到这里」过来时 URL 带 `?folder=<id>` → 用 URL 的；
 *   - 直接点左侧「上传文件」进来 → 落在根目录；
 *   - `?folder=` 是空串时按"没带"处理，否则上传区会带着空 id 卡住。
 */
export function resolveUploadTarget(
  requested: string | null | undefined,
  rootId: string | undefined,
): string | undefined {
  const trimmed = requested?.trim();
  return trimmed ? trimmed : rootId;
}
