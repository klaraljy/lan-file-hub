package com.lanfilehub.share;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * App 内下载的文件名解析（2026-10-03）。
 *
 * <p>为什么要单独一个类：WebView 自己不下载，我们把下载交给系统 DownloadManager
 * （见 {@link WebViewActivity#startSystemDownload}），而"存成什么名字"要从服务器给的
 * {@code Content-Disposition} 里取。**文件名不能直接当路径用** ——
 * 里面若带 {@code /}、{@code \}、{@code ..} 之类，塞进"下载"目录就是路径穿越。
 * 这些是纯函数，单测直接覆盖（不依赖 Android 运行时）。
 */
public final class DownloadNaming {

  /** 文件名里不允许出现的字符：路径分隔符与 Windows 保留字符 */
  private static final Pattern UNSAFE = Pattern.compile("[\\\\/:*?<>|]");
  private static final Pattern QUOTED = Pattern.compile("filename\\s*=\\s*\"([^\"]+)\"");
  private static final Pattern PLAIN = Pattern.compile("filename\\s*=\\s*([^;]+)");

  private DownloadNaming() {
  }

  /**
   * 从 Content-Disposition 里取文件名。
   * 支持 {@code attachment; filename="a.apk"} 与不带引号的写法；取不到返回 null。
   */
  public static String fromContentDisposition(String contentDisposition) {
    if (contentDisposition == null || contentDisposition.isEmpty()) {
      return null;
    }
    Matcher quoted = QUOTED.matcher(contentDisposition);
    if (quoted.find()) {
      return sanitize(quoted.group(1));
    }
    Matcher plain = PLAIN.matcher(contentDisposition);
    if (plain.find()) {
      return sanitize(plain.group(1).trim());
    }
    return null;
  }

  /** 去掉引号与危险字符；结果为空、或就是 . / .. 时返回 null（不能当文件名） */
  public static String sanitize(String raw) {
    if (raw == null) {
      return null;
    }
    String cleaned = UNSAFE.matcher(raw.trim().replace("\"", "")).replaceAll("_");
    if (cleaned.isEmpty() || cleaned.equals(".") || cleaned.equals("..")) {
      return null;
    }
    return cleaned;
  }

  /**
   * 兜底文件名：从 URL 末段猜一个（带票据的下载地址会退到这里）。
   * 永远返回一个可用的名字（最差是 {@code download}）。
   */
  public static String fromUrl(String downloadUrl) {
    if (downloadUrl == null || downloadUrl.isEmpty()) {
      return "download";
    }
    String path = downloadUrl;
    int query = path.indexOf('?');
    if (query >= 0) {
      path = path.substring(0, query);
    }
    int slash = path.lastIndexOf('/');
    String name = slash >= 0 ? path.substring(slash + 1) : path;
    String cleaned = sanitize(name);
    return cleaned == null ? "download" : cleaned;
  }
}
