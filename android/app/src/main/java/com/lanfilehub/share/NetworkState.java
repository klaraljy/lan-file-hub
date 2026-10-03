package com.lanfilehub.share;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;

/**
 * 当前网络的"人话"描述（用户 2026-10-03 要求：只显示 WiFi/热点/流量，不要 WiFi 名字）。
 *
 * <p>为什么刻意**不读 WiFi 名字（SSID）**：Android 10 起读 SSID 需要**定位权限**，
 * 为了在界面上显示一个名字去要定位权限不划算（用户 2026-10-03 明确选了"只显示 WiFi/热点/流量"）。
 *
 * <p>⛔ <b>判定必须以"本机有没有站点本地 IPv4"为主，不能只看"当前活动网络"</b>
 * —— 2026-10-03 实测踩过的坑：手机**自己开着热点**时，它的"活动网络"是**移动数据**
 * （手机自己的上网流量走蜂窝），热点接口只是一个 AP。原来的实现要求活动网络带
 * {@code NET_CAPABILITY_INTERNET} 且 transport 是 WiFi，于是这种情况被判成"流量"，
 * App 直接拒绝扫描 → 用户看到"找不到电脑"。正确做法：
 * <ol>
 *   <li>先看能不能拿到站点本地地址（{@link NetUtil#lanIp()}）：拿到就说明**在某个局域网里**
 *       （热点主机或 WiFi 客户端都算），能不能连电脑只看这一条；</li>
 *   <li>自己的地址是热点网关（192.168.43.1 之类）→ 显示"手机热点"；否则显示"WiFi"；</li>
 *   <li>拿不到局域网地址时才退回到"活动网络"判断（蜂窝 → 流量；都没有 → 没有网络）。</li>
 * </ol>
 */
public final class NetworkState {

  public enum Kind {
    NONE,
    WIFI,
    HOTSPOT,
    ETHERNET,
    CELLULAR,
    OTHER
  }

  private NetworkState() {
  }

  /** 读取当前网络类型（判定规则见类注释）。 */
  public static Kind detect(Context context) {
    boolean wifi = false;
    boolean ethernet = false;
    boolean cellular = false;
    try {
      ConnectivityManager manager =
          (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
      if (manager != null) {
        Network active = manager.getActiveNetwork();
        NetworkCapabilities capabilities = active == null ? null : manager.getNetworkCapabilities(active);
        if (capabilities != null) {
          wifi = capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI);
          ethernet = capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET);
          cellular = capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR);
        }
      }
    } catch (Exception ignored) {
      // 读系统服务失败不该让整个页面挂掉：下面按"有没有局域网地址"继续判断
    }
    return classify(wifi, ethernet, cellular, NetUtil.lanIp());
  }

  /**
   * 纯函数版判定（单测用）。
   *
   * <p>顺序：网线 → 有局域网地址（热点网关 = 手机热点，否则 WiFi）→ 只有蜂窝 → 其它/没有。
   *
   * @param ownIp 本机的站点本地地址；没有（例如纯流量）传 null 或空串
   */
  public static Kind classify(boolean wifi, boolean ethernet, boolean cellular, String ownIp) {
    if (ethernet) {
      return Kind.ETHERNET;
    }
    final boolean hasLanAddress = ownIp != null && !ownIp.isEmpty();
    if (hasLanAddress) {
      // 有局域网地址就是在局域网里：这才是"能不能连电脑"的判据。
      // 自己是热点网关 → 手机热点；否则是别人网络里的客户端（WiFi 一般如此）。
      return looksLikeHotspotGateway(ownIp) ? Kind.HOTSPOT : Kind.WIFI;
    }
    if (wifi) {
      return Kind.WIFI;  // 连上了 WiFi 但还没拿到地址（DHCP 中）
    }
    if (cellular) {
      return Kind.CELLULAR;
    }
    return Kind.NONE;
  }

  /** 兼容旧签名（早期只传"是不是热点网关"）：等价于"有局域网地址且是网关"。 */
  public static Kind classify(boolean wifi, boolean ethernet, boolean cellular,
                              boolean hotspotGateway) {
    return classify(wifi, ethernet, cellular, hotspotGateway ? "192.168.43.1" : null);
  }

  /** 本机在不在某个局域网里（拿到站点本地 IPv4 就算）。 */
  public static boolean hasLanAddress() {
    String ip = NetUtil.lanIp();
    return ip != null && !ip.isEmpty();
  }

  /**
   * 自己的地址是不是"热点网关"的典型取值（热点网段里的 .1）。
   *
   * <p>包含 Windows 移动热点（192.168.137.1）与常见安卓热点网段。
   *
   * <p>⛔ 必须**按完整地址比**，不能用 `startsWith("192.168.43.1")` —— 那样 `192.168.43.15`
   * 与 `192.168.43.100` 也会被当成网关（2026-10-03 写单测时发现的）。
   */
  public static boolean looksLikeHotspotGateway(String ownIp) {
    if (ownIp == null) {
      return false;
    }
    final String trimmed = ownIp.trim();
    return GATEWAY_ADDRESSES.contains(trimmed);
  }

  private static final java.util.Set<String> GATEWAY_ADDRESSES = new java.util.HashSet<>(
      java.util.Arrays.asList("192.168.43.1", "192.168.42.1", "192.168.44.1", "172.20.10.1",
          "192.168.137.1"));

  /** 这个网络能不能连到同一网络里的电脑：只要有局域网地址就行（含手机自己开热点）。 */
  public static boolean canReachComputer(Kind kind) {
    return kind == Kind.WIFI || kind == Kind.HOTSPOT || kind == Kind.ETHERNET || kind == Kind.OTHER;
  }
}
