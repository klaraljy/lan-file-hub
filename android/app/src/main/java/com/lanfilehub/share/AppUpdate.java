package com.lanfilehub.share;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

/**
 * App 内的「检查更新」（用户 2026-10-03 批准：不依赖网页，在连接页就能查/就更）。
 *
 * <p>为什么要有它：App 是"自带浏览器打开电脑网页"的形态，但**更新自己**这件事不能依赖网页 ——
 * 用户还没连上电脑、或者压根不想进网页时，也应该能问一句"有没有新版"。
 *
 * <p>后端的两个公开接口（都不需要登录，见 {@code backend/src/api/app_release_controller.cpp}）：
 *
 * <pre>
 *   GET /api/v1/app/release    {data:{version_code, version_name, size_bytes, built_at, download_path}}
 *   GET /api/v1/app/download   安装包本身（Content-Disposition: lan-file-hub.apk）
 * </pre>
 *
 * <p>⛔ <b>最后一跳一定是系统安装器的「安装」按钮，静默安装做不到</b>：侧载应用没有
 * {@code INSTALL_PACKAGES} 权限，申请设备管理员/MDM 会破坏"便携、不自作主张"的定位
 * （用户没同意，也不该做）。所以界面与提示都要把这一步说清楚，不能承诺"点一下自动装好"。
 *
 * <p>⛔ 这个类**不 import 任何 {@code android.*}**：JSON 解析与版本比较都能在电脑上用 JUnit 跑，
 * 与 {@link ShareHttpServer}、{@link BackupPlanner} 同一个路子（Android 的 {@code org.json}
 * 在 JVM 单测里是空壳，只有 {@code returnDefaultValues} 的默认值，解析出来全是空）。
 */
public final class AppUpdate {

  /** 公开的发布信息接口（无需登录） */
  public static final String RELEASE_PATH = "/api/v1/app/release";
  /** 安装包下载接口（无需登录） */
  public static final String DOWNLOAD_PATH = "/api/v1/app/download";

  private static final int CONNECT_TIMEOUT_MS = 3000;
  private static final int READ_TIMEOUT_MS = 8000;
  /** 安装包只有几百 KB，读进来毫无压力；给个上限防止拿到离谱响应把内存吃掉 */
  private static final int MAX_BODY_CHARS = 64 * 1024;

  private AppUpdate() {}

  /** 服务器上那个安装包的信息（字段名与后端返回的一模一样） */
  public static final class Release {
    public boolean available;
    public String fileName = "";
    public String versionName = "";
    public int versionCode;
    public long sizeBytes;
    public String builtAt = "";
    public String downloadPath = DOWNLOAD_PATH;

    /** 拼成这台电脑上的下载地址（后端给的是站内路径，也可能给整条 URL） */
    public String downloadUrl(String baseUrl) {
      String path = downloadPath == null || downloadPath.isEmpty() ? DOWNLOAD_PATH : downloadPath;
      if (path.startsWith("http://") || path.startsWith("https://")) {
        return path;
      }
      return (baseUrl == null ? "" : baseUrl) + (path.startsWith("/") ? path : ("/" + path));
    }

    /** 卡片上那行"版本 1.0 · 构建号 5 · 368 KB · 2026-10-03 18:40 构建"（缺什么就少显示什么） */
    public String describe() {
      StringBuilder text = new StringBuilder();
      if (!versionName.isEmpty()) {
        text.append("版本 ").append(versionName);
      }
      if (versionCode > 0) {
        if (text.length() > 0) {
          text.append(" · ");
        }
        text.append("构建号 ").append(versionCode);
      }
      String size = humanSize(sizeBytes);
      if (!size.isEmpty()) {
        if (text.length() > 0) {
          text.append(" · ");
        }
        text.append(size);
      }
      String built = humanTime(builtAt);
      if (!built.isEmpty()) {
        if (text.length() > 0) {
          text.append(" · ");
        }
        text.append(built).append(" 构建");
      }
      return text.toString();
    }
  }

  /** 检查更新的结果：给界面一句话，外加"能不能就更" */
  public static final class CheckResult {
    public final boolean ok;
    /** 界面直接显示的话（人话，不是错误码） */
    public final String message;
    /** 服务器上确实有更新包（按钮变成「更新到构建号 N」） */
    public final boolean updateAvailable;
    /** 有更新时的包信息；没有则为 null */
    public final Release release;

    private CheckResult(boolean ok, String message, boolean updateAvailable, Release release) {
      this.ok = ok;
      this.message = message;
      this.updateAvailable = updateAvailable;
      this.release = release;
    }
  }

  /**
   * 问一次电脑上有没有更新的安装包。
   *
   * <p>任何失败都变成一句人话（`ok=false`），**不抛异常**：这个按钮在首屏，网络不通是常态
   * （电脑没开、不在同一网络），不能让 App 崩或转圈。
   *
   * @param server  用哪台电脑（地址簿里最近用过/在线的那一条）
   * @param current 本机已装的构建号（{@code PackageInfo} 读出来的）
   */
  public static CheckResult check(ServerStore.Server server, int current) {
    if (server == null) {
      return new CheckResult(false, "还没有连过电脑。先点「找电脑」，或者手动输入电脑上显示的地址。", false,
          null);
    }
    String baseUrl = server.baseUrl();
    try {
      String body = get(baseUrl + RELEASE_PATH);
      Release release = parse(body);
      if (!release.available || release.versionCode <= 0) {
        return new CheckResult(false,
            "电脑上还没有安装包。在这台电脑上跑一次 scripts\\build-android.ps1 之后再试。", false, null);
      }
      if (hasNewerBuild(current, release.versionCode)) {
        return new CheckResult(true,
            "有新版：构建号 " + release.versionCode + "（现在是 " + current + "）—— 点下面「更新到构建号 "
                + release.versionCode + "」下载。",
            true, release);
      }
      return new CheckResult(true, "已是最新（构建号 " + current + "）。", false, release);
    } catch (IOException error) {
      return new CheckResult(false,
          "连不上这台电脑（" + baseUrl + "）。请确认电脑上的程序开着、手机和电脑在同一个 WiFi/热点。", false,
          null);
    }
  }

  /**
   * 服务器上的是不是更新的版本（纯函数，单测覆盖）。
   *
   * <p>读不到服务器版本号（0）+ 与本地一样 → 都算"已最新"：⛔ 不能在信息不全时把用户引去下载。
   */
  public static boolean hasNewerBuild(int current, int latest) {
    return latest > 0 && latest > current;
  }

  /**
   * 从 `/api/v1/app/release` 的响应体里取出发布信息（手写解析，见类注释为什么不用 org.json）。
   *
   * <p>容错优先：字段缺失、类型不对、整段不是 JSON，都退化成"没有安装包"而不是抛异常。
   * 只认**第一个**匹配的键（先出现的是外层 `data` 里的那个）。
   *
   * @param json 响应体，形如 {@code {"data":{...},"request_id":"..."}}；只看 `data` 那一段
   */
  public static Release parse(String json) {
    Release release = new Release();
    if (json == null || json.isEmpty()) {
      return release;
    }
    String data = firstObject(json, "data");
    if (data == null) {
      return release;
    }
    release.available = booleanValue(data, "available", false);
    release.fileName = stringValue(data, "file_name", "");
    release.versionName = stringValue(data, "version_name", "");
    release.versionCode = (int) longValue(data, "version_code", 0);
    release.sizeBytes = longValue(data, "size_bytes", 0);
    release.builtAt = stringValue(data, "built_at", "");
    String path = stringValue(data, "download_path", "");
    release.downloadPath = path.isEmpty() ? DOWNLOAD_PATH : path;
    return release;
  }

  /** 人类可读的体积：安装包只有几百 KB ~ 几 MB，所以两档就够 */
  public static String humanSize(long bytes) {
    if (bytes <= 0) {
      return "";
    }
    if (bytes < 1024 * 1024) {
      return (bytes / 1024) + " KB";
    }
    return String.format(Locale.US, "%.1f MB", bytes / 1024.0 / 1024.0);
  }

  /**
   * 后端给的 UTC 时间（{@code 2026-10-03T09:01:18Z}）→ 本地时间 {@code 2026-10-03 17:01}。
   * 认不出来就原样返回（宁可显示原始串，也不要显示 "Invalid Date"）。
   */
  public static String humanTime(String iso) {
    if (iso == null || iso.isEmpty()) {
      return "";
    }
    try {
      SimpleDateFormat parser = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.US);
      parser.setTimeZone(TimeZone.getTimeZone("UTC"));
      // 侧车里的秒后面可能带小数（构建脚本截到秒，这里仍然容错）
      String text = iso.endsWith("Z") ? iso.substring(0, iso.length() - 1) : iso;
      int dot = text.indexOf('.');
      if (dot > 0) {
        text = text.substring(0, dot);
      }
      Date parsed = parser.parse(text);
      if (parsed == null) {
        return iso;
      }
      SimpleDateFormat out = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US);
      return out.format(parsed);
    } catch (java.text.ParseException error) {
      return iso;
    }
  }

  /** 顶栏/提示里显示 host:port（整条 URL 对用户没意义） */
  public static String hostOf(String url) {
    if (url == null) {
      return "";
    }
    String text = url;
    if (text.startsWith("http://")) {
      text = text.substring("http://".length());
    } else if (text.startsWith("https://")) {
      text = text.substring("https://".length());
    }
    int slash = text.indexOf('/');
    if (slash >= 0) {
      text = text.substring(0, slash);
    }
    return text;
  }

  /** GET 一个**公开**接口（不带令牌：这两个接口本来就不需要登录） */
  private static String get(String url) throws IOException {
    HttpURLConnection connection = null;
    try {
      connection = (HttpURLConnection) new URL(url).openConnection();
      connection.setRequestMethod("GET");
      connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
      connection.setReadTimeout(READ_TIMEOUT_MS);
      connection.setUseCaches(false);
      connection.setRequestProperty("Accept", "application/json");
      int code = connection.getResponseCode();
      if (code < 200 || code >= 300) {
        throw new IOException("HTTP " + code);
      }
      return readBody(connection.getInputStream());
    } finally {
      if (connection != null) {
        connection.disconnect();
      }
    }
  }

  private static String readBody(InputStream stream) throws IOException {
    if (stream == null) {
      return "";
    }
    StringBuilder builder = new StringBuilder();
    try (BufferedReader reader =
             new BufferedReader(new InputStreamReader(stream, java.nio.charset.StandardCharsets.UTF_8))) {
      String line;
      while ((line = reader.readLine()) != null && builder.length() < MAX_BODY_CHARS) {
        builder.append(line);
      }
    }
    return builder.toString();
  }

  // ---- 极简 JSON 取值（只认后端这一种形状，纯 Java，可在电脑上单测）--------------

  /** 取 {@code {"key":{...}}} 里那个对象（含花括号本身）；没有就返回 null。 */
  static String firstObject(String json, String key) {
    int at = valueStart(json, key);
    if (at < 0 || at >= json.length() || json.charAt(at) != '{') {
      return null;
    }
    int depth = 0;
    boolean inString = false;
    boolean escaped = false;
    for (int index = at; index < json.length(); index++) {
      char current = json.charAt(index);
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (current == '\\') {
          escaped = true;
        } else if (current == '"') {
          inString = false;
        }
        continue;
      }
      if (current == '"') {
        inString = true;
      } else if (current == '{') {
        depth++;
      } else if (current == '}') {
        depth--;
        if (depth == 0) {
          return json.substring(at, index + 1);
        }
      }
    }
    return null;
  }

  /** 取字符串字段；不是字符串（或不存在）时用默认值。 */
  static String stringValue(String json, String key, String fallback) {
    int at = valueStart(json, key);
    if (at < 0 || at >= json.length() || json.charAt(at) != '"') {
      return fallback;
    }
    StringBuilder text = new StringBuilder();
    boolean escaped = false;
    for (int index = at + 1; index < json.length(); index++) {
      char current = json.charAt(index);
      if (escaped) {
        text.append(unescape(current));
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == '"') {
        return text.toString();
      } else {
        text.append(current);
      }
    }
    return fallback;
  }

  /** 取数字字段（布尔 true/false 也认，方便 available 复用）；取不到用默认值。 */
  static long longValue(String json, String key, long fallback) {
    int at = valueStart(json, key);
    if (at < 0) {
      return fallback;
    }
    int end = at;
    while (end < json.length()) {
      char current = json.charAt(end);
      if ((current >= '0' && current <= '9') || current == '-' || current == '+') {
        end++;
        continue;
      }
      break;
    }
    if (end == at) {
      return fallback;
    }
    try {
      return Long.parseLong(json.substring(at, end));
    } catch (NumberFormatException error) {
      return fallback;
    }
  }

  static boolean booleanValue(String json, String key, boolean fallback) {
    int at = valueStart(json, key);
    if (at < 0) {
      return fallback;
    }
    if (json.startsWith("true", at)) {
      return true;
    }
    if (json.startsWith("false", at)) {
      return false;
    }
    return longValue(json, key, fallback ? 1 : 0) != 0;
  }

  /** 找到 {@code "key"} 之后第一个非空白字符的位置（冒号、逗号都跳过）；没有这个键返回 -1。 */
  private static int valueStart(String json, String key) {
    if (json == null || key == null || key.isEmpty()) {
      return -1;
    }
    String needle = "\"" + key + "\"";
    int at = json.indexOf(needle);
    if (at < 0) {
      return -1;
    }
    int index = at + needle.length();
    while (index < json.length()) {
      char current = json.charAt(index);
      if (current == ':' || current == ' ' || current == '\t' || current == '\n' || current == '\r') {
        index++;
        continue;
      }
      return index;
    }
    return -1;
  }

  private static char unescape(char escaped) {
    switch (escaped) {
      case 'n':
        return '\n';
      case 't':
        return '\t';
      case 'r':
        return '\r';
      case 'b':
        return '\b';
      case 'f':
        return '\f';
      default:
        // 反斜杠开头的转义只处理上面几个：文件名与路径里用不到 unicode 转义，保持简单
        return escaped;
    }
  }
}
