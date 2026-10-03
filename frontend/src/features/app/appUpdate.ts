/**
 * 「我在不在 App 里、装的是哪个构建号」（2026-10-03）。
 *
 * 用户要求：「这个 app 里面的 apk 下载你就直接改成更新吧」——
 * 在 App 里再喊「下载安装」很奇怪（已经装了），应该告诉用户"有没有新版、点一下更新"。
 *
 * App 的 WebView 在 UA 末尾加了 `LanFileHubApp/<versionCode>`
 * （见 android 的 `WebViewActivity.UA_MARKER`），网页据此判断：
 *   - 没这段标记 → 普通浏览器，按"下载安装"处理；
 *   - 有这段标记 → 在 App 里，拿已装构建号与服务器上的最新构建号比较。
 *
 * 纯函数，单测覆盖（UA 解析错了要么白提示、要么把用户引到错误按钮上）。
 */

/** App 在 UA 里留下的标记前缀 */
export const APP_UA_MARKER = 'LanFileHubApp/';

/**
 * 从 UA 里读出已装的构建号；不是 App 内打开、或读不到数字时返回 null。
 * 例：`Mozilla/5.0 (Linux; Android 13) ... Chrome/120 Mobile Safari/537.36 LanFileHubApp/4`
 */
export function installedBuildFromUserAgent(userAgent: string | undefined | null): number | null {
  if (!userAgent) return null;
  const index = userAgent.indexOf(APP_UA_MARKER);
  if (index < 0) return null;
  const rest = userAgent.slice(index + APP_UA_MARKER.length);
  const match = /^(\d+)/.exec(rest);
  if (!match) return null;
  const code = Number.parseInt(match[1]!, 10);
  return Number.isFinite(code) && code > 0 ? code : null;
}

export type AppUpdateState = 'unknown' | 'install' | 'latest' | 'update';

/**
 * 该给用户看什么动作：
 *   - `install`：不在 App 里（普通浏览器）或读不到已装号 → 「下载安装」；
 *   - `latest` ：在 App 里，且已装号 >= 服务器上的最新号 → 「已是最新」；
 *   - `update` ：在 App 里，且服务器上的号更大 → 「更新到构建号 N」。
 */
export function appUpdateState(installed: number | null, latest: number | undefined): AppUpdateState {
  if (installed === null || !latest || latest <= 0) return 'install';
  return latest > installed ? 'update' : 'latest';
}

/**
 * 在 App 里打开时，「记住我」必须是**强制勾上且不能取消**（2026-10-03 用户批准的交接单 ③）。
 *
 * 用户口径：「App 内默认记住登录」——手机 App 每天都要重新输口令是产品缺陷（AGENTS §14.1）。
 * 判断依据就是 UA 里那段 `LanFileHubApp/<versionCode>`：认不出来 = 普通浏览器，
 * 老老实实保留勾选框（公用电脑上用户要能取消）。
 */
export function rememberLockedInApp(userAgent: string | undefined | null): boolean {
  return installedBuildFromUserAgent(userAgent) !== null;
}
