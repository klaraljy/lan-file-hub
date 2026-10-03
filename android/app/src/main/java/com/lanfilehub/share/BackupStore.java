package com.lanfilehub.share;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONException;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * 相册备份的本地设置与"已备份清单"（2026-10-03）。
 *
 * <p>存什么：服务器地址与登录令牌、要备份的相册、是否仅 WiFi、已备份照片的键值集合。
 * <p>⛔ 只存令牌，**不存口令**（与网页端同一条规矩，docs/security.md）。
 * <p>为什么"已备份清单"放本地：手机上百万张照片不可能每次问服务器 —— 服务器只按名字判重，
 * 而本地的键含大小与拍摄时间，判断更准。清单只增不减（照片删了也无所谓，键留着不占地方）。
 */
public final class BackupStore {

  private static final String PREFS = "lan_file_hub_backup";
  private static final String KEY_HOST = "host";
  private static final String KEY_PORT = "port";
  private static final String KEY_TOKEN = "token";
  private static final String KEY_USER = "username";
  private static final String KEY_BUCKETS = "buckets";
  private static final String KEY_WIFI_ONLY = "wifi_only";
  private static final String KEY_BACKED_UP = "backed_up_keys";
  private static final String KEY_LAST_RESULT = "last_result";
  private static final String KEY_LAST_RUN = "last_run";

  private BackupStore() {}

  private static SharedPreferences prefs(Context context) {
    return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
  }

  /** 服务器 + 令牌是否已经填好（没有就先去「连接电脑」连一次并登录） */
  public static boolean isConfigured(Context context) {
    SharedPreferences store = prefs(context);
    return store.getInt(KEY_PORT, 0) > 0 && !store.getString(KEY_TOKEN, "").isEmpty();
  }

  public static void saveLogin(Context context, ServerStore.Server server, String username,
                               String token) {
    prefs(context)
        .edit()
        .putString(KEY_HOST, server.host)
        .putInt(KEY_PORT, server.port)
        .putString(KEY_USER, username)
        .putString(KEY_TOKEN, token)
        .apply();
  }

  public static String host(Context context) {
    return prefs(context).getString(KEY_HOST, "");
  }

  public static int port(Context context) {
    return prefs(context).getInt(KEY_PORT, 0);
  }

  public static String username(Context context) {
    return prefs(context).getString(KEY_USER, "");
  }

  public static String token(Context context) {
    return prefs(context).getString(KEY_TOKEN, "");
  }

  public static void clearLogin(Context context) {
    prefs(context).edit().remove(KEY_TOKEN).remove(KEY_USER).apply();
  }

  public static boolean wifiOnly(Context context) {
    return prefs(context).getBoolean(KEY_WIFI_ONLY, true);
  }

  public static void setWifiOnly(Context context, boolean wifiOnly) {
    prefs(context).edit().putBoolean(KEY_WIFI_ONLY, wifiOnly).apply();
  }

  /** 用户勾选的相册名（空的 = 还没选过，界面上要提示"先选相册"） */
  public static Set<String> buckets(Context context) {
    return new HashSet<>(prefs(context).getStringSet(KEY_BUCKETS, new HashSet<String>()));
  }

  public static void setBuckets(Context context, Set<String> buckets) {
    prefs(context).edit().putStringSet(KEY_BUCKETS, new HashSet<>(buckets)).apply();
  }

  /** 已备份的照片键（见 BackupPlanner.photoKey） */
  public static Set<String> backedUpKeys(Context context) {
    String raw = prefs(context).getString(KEY_BACKED_UP, "[]");
    Set<String> keys = new HashSet<>();
    try {
      JSONArray array = new JSONArray(raw);
      for (int index = 0; index < array.length(); index++) {
        keys.add(array.getString(index));
      }
    } catch (JSONException ignored) {
      // 存储坏了就当"什么都没备份过"：最多重传一次，不会漏传
    }
    return keys;
  }

  /** 追加已备份的键（批量写，避免每张照片都刷一次盘） */
  public static void addBackedUpKeys(Context context, List<String> keys) {
    if (keys == null || keys.isEmpty()) {
      return;
    }
    Set<String> all = backedUpKeys(context);
    all.addAll(keys);
    JSONArray array = new JSONArray();
    for (String key : all) {
      array.put(key);
    }
    prefs(context).edit().putString(KEY_BACKED_UP, array.toString()).apply();
  }

  public static List<String> backedUpKeyList(Context context) {
    return new ArrayList<>(backedUpKeys(context));
  }

  public static void setLastResult(Context context, String text) {
    prefs(context)
        .edit()
        .putString(KEY_LAST_RESULT, text)
        .putLong(KEY_LAST_RUN, System.currentTimeMillis())
        .apply();
  }

  public static String lastResult(Context context) {
    return prefs(context).getString(KEY_LAST_RESULT, "");
  }

  public static long lastRun(Context context) {
    return prefs(context).getLong(KEY_LAST_RUN, 0L);
  }
}
