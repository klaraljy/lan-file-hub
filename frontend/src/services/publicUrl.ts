/**
 * 分享链接的对外地址（AGENTS.md §14.4）。
 *
 * 为什么需要单独一层：链接过去是按「当前访问地址」拼的，用户在电脑上用 `127.0.0.1:5173`
 * 打开页面时，生成的链接就是 `http://127.0.0.1:5173/s/<token>` ——
 * **手机上 127.0.0.1 指的是手机自己，必然打不开**（用户实测踩到过）。
 *
 * 规则：
 *   1. 本来就是局域网地址 / 域名访问 → 直接用当前 origin，没问题；
 *   2. 用本机地址打开、且启动脚本注入了局域网地址 → 换成局域网地址，并提示已经换过；
 *   3. 用本机地址打开、又没有局域网地址 → 只能给本机地址，但必须警告"别人手机打不开"。
 */

const LOCAL_HOSTNAMES = new Set(['localhost', '127.0.0.1', '::1', '[::1]']);

/** 只取判断需要的两个字段，注入进来是为了能单测（jsdom 的 location 改不动） */
export interface ShareUrlLocation {
  origin: string;
  hostname: string;
}

export interface ShareUrlResult {
  url: string;
  /** 需要提醒用户时给文案；正常情况下没有 */
  warning?: string;
}

/** 局域网地址由 scripts/start.ps1 通过 vite 的 define 注入；没注入时是空字符串 */
function injectedLanUrl(): string {
  const raw = import.meta.env.VITE_LFH_LAN_URL;
  if (typeof raw !== 'string') return '';
  return raw.trim().replace(/\/+$/, '');
}

/** 拼出"发给别人、别人手机能打开"的分享链接 */
export function buildShareUrl(
  token: string,
  location: ShareUrlLocation = window.location,
): ShareUrlResult {
  const path = `/s/${token}`;
  const { origin, hostname } = location;

  if (!LOCAL_HOSTNAMES.has(hostname)) {
    return { url: `${origin}${path}` };
  }

  const lanUrl = injectedLanUrl();
  if (lanUrl) {
    return {
      url: `${lanUrl}${path}`,
      warning: '你正在用本机地址（127.0.0.1）打开页面，已自动换成局域网地址 —— 这样别人手机才能打开。',
    };
  }

  return {
    url: `${origin}${path}`,
    warning:
      '这条链接在别人手机上打不开。请改用局域网地址重新打开本页面（启动脚本会打印「📱 手机访问」那个地址），再生成链接。',
  };
}
