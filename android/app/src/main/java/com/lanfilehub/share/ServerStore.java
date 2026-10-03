package com.lanfilehub.share;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * 「我连过哪些电脑」的本地记录（用户 2026-10-02 要求：App 里要有历史 IP，能选一个连，并显示在不在线）。
 *
 * <p>只存在手机本地的 SharedPreferences 里，不上传、不同步 —— 换手机就没了，这是刻意的（简单、
 * 不引入账号体系）。
 */
public final class ServerStore {

  private static final String PREFS = "lan_file_hub_servers";
  private static final String KEY_SERVERS = "servers";

  /** 一台被记住的电脑。 */
  public static final class Server {
    public String host;
    public int port;
    public String label;
    public long lastUsedAt;

    public Server(String host, int port, String label, long lastUsedAt) {
      this.host = host;
      this.port = port;
      this.label = label;
      this.lastUsedAt = lastUsedAt;
    }

    /** 浏览器里打开的地址（单端口形态：网页与接口在同一个端口） */
    public String baseUrl() {
      return "http://" + host + ":" + port;
    }

    public String displayName() {
      return (label == null || label.isEmpty()) ? host : label;
    }

    public String key() {
      return host + ":" + port;
    }
  }

  private ServerStore() {}

  /** 后端默认端口（便携包形态；自动查找电脑时按这个端口扫） */
  public static final int DEFAULT_PORT = 9180;

  /** 便携包默认端口；用户存过带端口的地址时，自动查找也用那个端口（同一个后端只有一个端口） */
  public static int defaultPort(android.content.Context context) {
    for (Server server : load(context)) {
      if (server.port > 0) {
        return server.port;
      }
    }
    return DEFAULT_PORT;
  }

  /** 按 host:port 找一条已知记录（自动查找时用来判断"这台是不是新的"）；没有返回 null。 */
  public static Server find(android.content.Context context, String host, int port) {
    for (Server server : load(context)) {
      if (server.port == port && server.host != null && server.host.equals(host)) {
        return server;
      }
    }
    return null;
  }

  /**
   * 把用户输入变成一台服务器的信息。允许只输 IP（最常见），也允许带端口或整条 URL：
   *
   * <pre>
   *   192.168.43.240            → http://192.168.43.240:9180
   *   192.168.43.240:9180       → 同上
   *   http://10.0.0.5/          → http://10.0.0.5:9180
   * </pre>
   *
   * 解析不出来返回 null（调用方提示"地址看起来不对"）。
   */
  public static Server parse(String rawInput) {
    if (rawInput == null) {
      return null;
    }
    String text = rawInput.trim();
    if (text.isEmpty()) {
      return null;
    }
    if (text.startsWith("http://")) {
      text = text.substring("http://".length());
    } else if (text.startsWith("https://")) {
      text = text.substring("https://".length());
    }
    final int slash = text.indexOf('/');
    if (slash >= 0) {
      text = text.substring(0, slash);
    }
    String host = text;
    int port = 9180; // 便携包默认端口（可在设置里改）
    final int colon = text.lastIndexOf(':');
    if (colon > 0) {
      host = text.substring(0, colon);
      try {
        port = Integer.parseInt(text.substring(colon + 1));
      } catch (NumberFormatException error) {
        return null;
      }
      if (port < 1 || port > 65535) {
        return null;
      }
    }
    if (host.isEmpty() || host.contains(" ")) {
      return null;
    }
    return new Server(host, port, "", System.currentTimeMillis());
  }

  public static List<Server> load(Context context) {
    SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    String raw = prefs.getString(KEY_SERVERS, "[]");
    List<Server> servers = new ArrayList<>();
    try {
      JSONArray array = new JSONArray(raw);
      for (int index = 0; index < array.length(); index++) {
        JSONObject item = array.getJSONObject(index);
        servers.add(
            new Server(
                item.getString("host"),
                item.getInt("port"),
                item.optString("label", ""),
                item.optLong("lastUsedAt", 0)));
      }
    } catch (JSONException ignored) {
      // 记录坏了就当没有（不因为一条脏数据让整个页面打不开）
    }
    // 最近用过的排前面 —— 用户最可能点的就是它
    Collections.sort(
        servers,
        new Comparator<Server>() {
          @Override
          public int compare(Server left, Server right) {
            return Long.compare(right.lastUsedAt, left.lastUsedAt);
          }
        });
    return servers;
  }

  public static void save(Context context, List<Server> servers) {
    JSONArray array = new JSONArray();
    for (Server server : servers) {
      JSONObject item = new JSONObject();
      try {
        item.put("host", server.host);
        item.put("port", server.port);
        item.put("label", server.label == null ? "" : server.label);
        item.put("lastUsedAt", server.lastUsedAt);
      } catch (JSONException ignored) {
        continue;
      }
      array.put(item);
    }
    context
        .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        .edit()
        .putString(KEY_SERVERS, array.toString())
        .apply();
  }

  /** 加一台（同 host:port 视为同一台，只更新名字与时间）。 */
  public static List<Server> addOrUpdate(Context context, Server server) {
    List<Server> servers = load(context);
    for (Server existing : servers) {
      if (existing.key().equals(server.key())) {
        existing.label = server.label;
        existing.lastUsedAt = System.currentTimeMillis();
        save(context, servers);
        return servers;
      }
    }
    servers.add(server);
    save(context, servers);
    return servers;
  }

  public static List<Server> remove(Context context, Server server) {
    List<Server> servers = load(context);
    List<Server> kept = new ArrayList<>();
    for (Server existing : servers) {
      if (!existing.key().equals(server.key())) {
        kept.add(existing);
      }
    }
    save(context, kept);
    return kept;
  }

  public static void touch(Context context, Server server) {
    List<Server> servers = load(context);
    for (Server existing : servers) {
      if (existing.key().equals(server.key())) {
        existing.lastUsedAt = System.currentTimeMillis();
      }
    }
    save(context, servers);
  }
}
