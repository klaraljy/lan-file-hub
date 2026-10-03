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

  /**
   * 本机所在网段的掩码前缀长度（例如 24 = /24）。拿不到返回 24。
   *
   * <p>为什么要它（2026-10-03 用户实测：校园网能连但「找电脑」找不到）：
   * 自动发现只扫**本机那一段**的 254 个地址（见 {@link ServerDiscovery}），
   * 而校园网常见 /16 大网段（6 万多个地址，例如本机 10.200.77.x、电脑 10.200.18.61）——
   * 两者不在同一 /24 里，扫描必然找不到，但这**不是网络不允许互访**。
   * 界面据此给出正确的下一步（手动填地址），而不是一律说"客户端隔离，改用热点"。
   */
  public static int prefixLength() {
    try {
      Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
      while (interfaces != null && interfaces.hasMoreElements()) {
        NetworkInterface nif = interfaces.nextElement();
        if (!nif.isUp() || nif.isLoopback()) {
          continue;
        }
        String name = nif.getName() == null ? "" : nif.getName().toLowerCase(Locale.ROOT);
        boolean preferred = name.startsWith("wlan") || name.startsWith("ap")
            || name.startsWith("swlan") || name.startsWith("eth");
        if (!preferred) {
          continue;
        }
        for (InterfaceAddress address : nif.getInterfaceAddresses()) {
          InetAddress ip = address.getAddress();
          if (ip instanceof Inet4Address && ip.isSiteLocalAddress()) {
            int length = address.getNetworkPrefixLength();
            return length > 0 ? length : 24;
          }
        }
      }
    } catch (SocketException e) {
      return 24;
    }
    return 24;
  }

  /** 网段是不是比 /24 更宽（大网段：手机只扫自己那一段就很可能找不到电脑）。 */
  public static boolean isWideSubnet(int prefixLength) {
    return prefixLength > 0 && prefixLength < 24;
  }
}
