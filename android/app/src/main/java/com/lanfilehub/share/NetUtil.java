package com.lanfilehub.share;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.Enumeration;
import java.util.Locale;

/** 找出本机在局域网/热点里的 IPv4 地址（qr 与文字都靠它）。 */
public final class NetUtil {

  private NetUtil() {
  }

  /**
   * 返回最可能被对方访问到的 IPv4 地址：优先 wlan/ap/eth 这类真实接口，其次任意站点本地地址。
   * 找不到（例如没连网）返回 null。
   */
  public static String lanIp() {
    try {
      String fallback = null;
      Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
      while (interfaces != null && interfaces.hasMoreElements()) {
        NetworkInterface nif = interfaces.nextElement();
        if (!nif.isUp() || nif.isLoopback()) {
          continue;
        }
        String name = nif.getName() == null ? "" : nif.getName().toLowerCase(Locale.ROOT);
        boolean preferred = name.startsWith("wlan") || name.startsWith("ap")
            || name.startsWith("swlan") || name.startsWith("eth");
        for (InterfaceAddress address : nif.getInterfaceAddresses()) {
          InetAddress ip = address.getAddress();
          if (ip instanceof Inet4Address && ip.isSiteLocalAddress()) {
            if (preferred) {
              return ip.getHostAddress();
            }
            if (fallback == null) {
              fallback = ip.getHostAddress();
            }
          }
        }
      }
      return fallback;
    } catch (SocketException e) {
      return null;
    }
  }

  /** 是否处在"手机热点自己当网关"的典型网段（提示文案会用到）。 */
  public static boolean looksLikeHotspot(String ip) {
    return ip != null && (ip.startsWith("192.168.43.") || ip.startsWith("192.168.42.")
        || ip.startsWith("192.168.44.") || ip.startsWith("172.20.10."));
  }
}
