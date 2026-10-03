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
 * <p>热点怎么判断：自己就是网关（自己的 IPv4 落在 192.168.42/43/44.1、172.20.10.1 这些
 * 常见热点网段）→ 说明"这台手机正在开热点"。判断逻辑放在静态纯函数里，方便在电脑上跑单测。
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

  /** 读取当前活动网络的类型（没有网络时返回 NONE）。 */
  public static Kind detect(Context context) {
    ConnectivityManager manager =
        (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
    if (manager == null) {
      return Kind.NONE;
    }
    Network active = manager.getActiveNetwork();
    if (active == null) {
      return Kind.NONE;
    }
    NetworkCapabilities capabilities = manager.getNetworkCapabilities(active);
    if (capabilities == null || !capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) {
      return Kind.NONE;
    }
    boolean wifi = capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI);
    boolean ethernet = capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET);
    boolean cellular = capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR);
    return classify(wifi, ethernet, cellular, looksLikeHotspotGateway(NetUtil.lanIp()));
  }

  /**
   * 纯函数版判定（单测用）：WiFi + 自己是热点网关 = 手机热点。
   *
   * <p>顺序很重要：先看有线，再看"WiFi 且自己是网关"，最后才是普通 WiFi 与流量。
   */
  public static Kind classify(boolean wifi, boolean ethernet, boolean cellular,
                              boolean hotspotGateway) {
    if (ethernet) {
      return Kind.ETHERNET;
    }
    if (wifi && hotspotGateway) {
      return Kind.HOTSPOT;
    }
    if (wifi) {
      return Kind.WIFI;
    }
    if (cellular) {
      return Kind.CELLULAR;
    }
    return Kind.OTHER;
  }

  /** 自己的地址是不是"热点网关"的典型取值（热点网段里的 .1）。 */
  public static boolean looksLikeHotspotGateway(String ownIp) {
    return ownIp != null && (ownIp.startsWith("192.168.43.1") || ownIp.startsWith("192.168.42.1")
        || ownIp.startsWith("192.168.44.1") || ownIp.startsWith("172.20.10.1"));
  }

  /** 这个网络能不能连到同一网络里的电脑：只有 WiFi / 热点 / 网线可以，流量不行。 */
  public static boolean canReachComputer(Kind kind) {
    return kind == Kind.WIFI || kind == Kind.HOTSPOT || kind == Kind.ETHERNET;
  }
}
