/**
 * 分享链接的对外地址（AGENTS.md §14.4）。
 *
 * 为什么需要单独一层：链接过去是按「当前访问地址」拼的，用户在电脑上用 `127.0.0.1:5173`
 * 打开页面时，生成的链接就是 `http://127.0.0.1:5173/s/<token>` ——
 * **手机上 127.0.0.1 指的是手机自己，必然打不开**（用户实测踩到过）。
 *
 * 规则：
 *   1. 本来就是局域网地址 / 域名访问 → 直接用当前 origin，没问题；
 *   2. 用本机地址打开、且**服务端上报了当前局域网地址** → 换成它（换网络后立刻生效，
 *      不用重启；见 `services/networkApi.ts`）；
 *   3. 用本机地址打开、服务端没给（拉取失败），但启动脚本注入了局域网地址 → 用注入值；
 *   4. 都没有 → 只能给本机地址，但必须警告"别人手机打不开"。
 */

const LOCAL_HOSTNAMES = new Set(['localhost', '127.0.0.1', '::1', '[::1]']);

/**
 * 服务端上报的"当前局域网地址"（只有 IP，没有协议/端口）。
 * 由 `services/networkApi.ts` 在打开 App、定时与窗口获得焦点时刷新，
 * 所以用户换网络之后这里会自动变成新地址。
 */
let serverLanAddress = '';

/**
 * 订阅"局域网地址变了"（2026-10-03「手机怎么连」卡片用）。
 * 换网络后卡片上的地址和二维码要跟着变，用户不用刷新页面。
 * 返回取消订阅的函数。
 */
const lanListeners = new Set<(address: string) => void>();

export function subscribeServerLanAddress(listener: (address: string) => void): () => void {
  lanListeners.add(listener);
  return () => {
    lanListeners.delete(listener);
  };
}

/** 写入服务端上报的地址（空串 = 服务端没探到，例如没有默认路由） */
export function setServerLanAddress(address: string): void {
  const next = address.trim();
  if (next === serverLanAddress) {
    return;
  }
  serverLanAddress = next;
  for (const listener of lanListeners) {
    listener(next);
  }
}

export function getServerLanAddress(): string {
  return serverLanAddress;
}

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

/**
 * 用"服务端上报的当前局域网 IP + 当前页面的协议与端口"拼出基地址。
 * 端口取当前页面自己的（便携包 9180、开发 5173），所以两种形态都正确。
 */
function serverLanBase(location: ShareUrlLocation): string {
  if (!serverLanAddress) return '';
  try {
    const { protocol, port } = new URL(location.origin);
    return `${protocol}//${serverLanAddress}${port ? `:${port}` : ''}`;
  } catch {
    return '';
  }
}

/** 拼出"发给别人、别人手机能打开"的分享链接 */
export function buildShareUrl(
  token: string,
  location: ShareUrlLocation = window.location,
): ShareUrlResult {
  const { base, warning } = resolvePublicBase(location);
  return { url: `${base}/s/${token}`, warning };
}

/**
 * 拼出邀请注册链接（`/register?invite=<token>`）。
 * 与分享链接**同一套"手机上点得开"的规则**（用户 2026-10-02：不是所有人都能注册，
 * 要靠管理员发这条链接），所以共用 resolvePublicBase，别各写一套。
 */
export function buildInviteUrl(
  token: string,
  location: ShareUrlLocation = window.location,
): ShareUrlResult {
  const { base, warning } = resolvePublicBase(location);
  return { url: `${base}/register?invite=${encodeURIComponent(token)}`, warning };
}

/**
 * 拼出"配套手机应用"的下载地址（用户 2026-10-03）。
 *
 * 同一个道理：电脑上用 127.0.0.1 打开时，链接/二维码必须是局域网地址，
 * 手机扫了才装得上。所以这里也走 resolvePublicBase，**不要再自己拼一套**。
 */
export function buildAppDownloadUrl(
  downloadPath: string,
  location: ShareUrlLocation = window.location,
): ShareUrlResult {
  const { base, warning } = resolvePublicBase(location);
  const path = downloadPath.startsWith('/') ? downloadPath : `/${downloadPath}`;
  return { url: `${base}${path}`, warning };
}

/**
 * 拼出"手机打开本系统"的地址（2026-10-03「手机怎么连」卡片用）。
 *
 * ⛔ 不要图省事用 `window.location.origin`：电脑上用 127.0.0.1 打开时，
 * 那个地址在手机上指的是手机自己，扫码必然打不开（AGENTS §14.4 的同一个坑）。
 * 这里与分享链接共用 resolvePublicBase —— 换网络后地址会自动更新。
 */
export function buildSiteUrl(location: ShareUrlLocation = window.location): ShareUrlResult {
  const { base, warning } = resolvePublicBase(location);
  return { url: `${base}/`, warning };
}

/** 对外基地址（不带路径）+ 需要提醒用户时的文案 */
interface PublicBase {
  base: string;
  warning?: string;
}

/** 分享链接与邀请链接共用的"对外基地址" */
function resolvePublicBase(location: ShareUrlLocation): PublicBase {
  const { origin, hostname } = location;

  if (!LOCAL_HOSTNAMES.has(hostname)) {
    return { base: origin };
  }

  const fromServer = serverLanBase(location);
  if (fromServer) {
    return {
      base: fromServer,
      warning: `你正在用本机地址（127.0.0.1）打开页面，已自动换成当前的局域网地址（${serverLanAddress}）—— 这样别人手机才能打开；换网络后这个地址会自动更新，不用重启。`,
    };
  }

  const lanUrl = injectedLanUrl();
  if (lanUrl) {
    return {
      base: lanUrl,
      warning: '你正在用本机地址（127.0.0.1）打开页面，已自动换成局域网地址 —— 这样别人手机才能打开。',
    };
  }

  return {
    base: origin,
    warning:
      '这条链接在别人手机上打不开。请改用局域网地址重新打开本页面（启动脚本会打印「手机打开」那个地址），再生成链接。',
  };
}
