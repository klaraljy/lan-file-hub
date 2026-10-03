package com.lanfilehub.share;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * 探一台电脑在不在线：请求 `/health/live`（后端就绪即返回 200，不需要登录）。
 *
 * <p>为什么要探活（用户 2026-10-02 要求）：换网络之后旧地址就死了，用户需要一眼看出
 * "哪个地址现在能用"，而不是点进去等半天白屏。所以列表里每条都显示在线/离线。
 *
 * <p>刻意用 {@link HttpURLConnection}：不引第三方库，超时自己控，失败一定是"离线"而不是抛异常。
 */
public final class ServerProbe {

  private ServerProbe() {}

  /** 超时给短一点（1.5 秒）：列表一次要探好几条，用户等不起。 */
  public static boolean isOnline(ServerStore.Server server) {
    HttpURLConnection connection = null;
    try {
      URL url = new URL(server.baseUrl() + "/health/live");
      connection = (HttpURLConnection) url.openConnection();
      connection.setRequestMethod("GET");
      connection.setConnectTimeout(1500);
      connection.setReadTimeout(1500);
      connection.setUseCaches(false);
      int code = connection.getResponseCode();
      return code == 200;
    } catch (IOException error) {
      return false;
    } finally {
      if (connection != null) {
        connection.disconnect();
      }
    }
  }
}
