package com.lanfilehub.share;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URLDecoder;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/**
 * 「局域网快传」内置的极简 HTTP 服务（五阶段改造 ③）。
 *
 * <p>为什么自己写而不用框架：手机 → 手机 ≤ 1 GB 的场景只需要三件事 ——
 * <b>列文件、下载（要支持断点续传）、上传</b>。自己写一个 HTTP/1.1 子集实现，
 * 不引入任何服务端依赖，而且**这个类是纯 Java（不 import 任何 android.*）**，
 * 所以可以在电脑上直接用 JUnit 跑起来验证（见 {@code ShareHttpServerTest}）。
 *
 * <p>路由：
 * <pre>
 *   GET  /                  文件列表 + 上传表单（中文、手机友好）
 *   GET  /f/&lt;文件名&gt;        下载（支持 Range 断点续传）
 *   POST /upload?name=xxx   上传（请求体就是文件内容，浏览器 fetch 直接发）
 *   GET  /health            探活（返回 ok）
 * </pre>
 *
 * <p>安全：文件名只允许"纯文件名"，任何 {@code ..}、路径分隔符、绝对路径都被拒绝，
 * 并且写入/读取前都用规范化路径再确认一次落在分享目录内（防止目录穿越）。
 */
public final class ShareHttpServer {

  /** 日志出口：Android 侧接到 Logcat，测试里接到 System.out */
  public interface Logger {
    void log(String message);
  }

  private static final int BUFFER_SIZE = 64 * 1024;
  /** 单个上传上限：用户口径是手机 → 手机 ≤ 1 GB，这里留一倍余量 */
  private static final long MAX_UPLOAD_BYTES = 2L * 1024 * 1024 * 1024;
  private static final int MAX_HEADER_LINES = 100;

  private final File shareDir;
  private final int preferredPort;
  private final Logger logger;

  private ServerSocket serverSocket;
  private ExecutorService workers;
  private Thread acceptThread;
  private volatile boolean running;
  private volatile int port;

  public ShareHttpServer(File shareDir, int preferredPort, Logger logger) {
    this.shareDir = shareDir;
    this.preferredPort = preferredPort;
    this.logger = logger != null ? logger : message -> { };
  }

  /** 启动服务。固定端口被占用时自动改用系统分配的端口（保证一定有服务可用）。 */
  public synchronized void start() throws IOException {
    if (running) {
      return;
    }
    if (!shareDir.exists() && !shareDir.mkdirs()) {
      throw new IOException("无法创建分享目录：" + shareDir.getAbsolutePath());
    }

    ServerSocket socket = null;
    try {
      socket = new ServerSocket(preferredPort, 32);
    } catch (IOException ignored) {
      // 端口被占：退回系统分配，端口号通过 getPort() 告诉界面
      socket = new ServerSocket(0, 32);
    }
    serverSocket = socket;
    port = socket.getLocalPort();
    running = true;
    workers = Executors.newFixedThreadPool(4, runnable -> {
      Thread thread = new Thread(runnable, "share-http");
      thread.setDaemon(true);
      return thread;
    });
    acceptThread = new Thread(this::acceptLoop, "share-accept");
    acceptThread.setDaemon(true);
    acceptThread.start();
    logger.log("分享服务已启动 | 端口=" + port + " | 目录=" + shareDir.getAbsolutePath());
  }

  /** 停止服务（幂等）。 */
  public synchronized void stop() {
    if (!running) {
      return;
    }
    running = false;
    closeQuietly(serverSocket);
    if (workers != null) {
      workers.shutdownNow();
    }
    logger.log("分享服务已停止");
  }

  public boolean isRunning() {
    return running;
  }

  /** 实际监听端口（start() 之后才有效） */
  public int getPort() {
    return port;
  }

  public File getShareDir() {
    return shareDir;
  }

  /** 按修改时间倒序列出可分享的文件（跳过子目录与隐藏文件） */
  public List<File> listFiles() {
    File[] entries = shareDir.listFiles();
    List<File> files = new ArrayList<>();
    if (entries != null) {
      for (File entry : entries) {
        if (entry.isFile() && !entry.getName().startsWith(".")) {
          files.add(entry);
        }
      }
    }
    files.sort(Comparator.comparingLong(File::lastModified).reversed());
    return files;
  }

  // ---------------------------------------------------------------- 请求处理

  private void acceptLoop() {
    while (running) {
      Socket client;
      try {
        client = serverSocket.accept();
      } catch (IOException e) {
        if (running) {
          logger.log("接受连接失败：" + e.getMessage());
        }
        break;
      }
      try {
        workers.execute(() -> handle(client));
      } catch (RejectedExecutionException e) {
        closeQuietly(client);
      }
    }
  }

  private void handle(Socket client) {
    try (Socket socket = client;
         InputStream rawInput = new BufferedInputStream(socket.getInputStream(), BUFFER_SIZE);
         OutputStream output = new BufferedOutputStream(socket.getOutputStream(), BUFFER_SIZE)) {
      socket.setSoTimeout(15000);

      String requestLine = readLine(rawInput);
      if (requestLine == null || requestLine.isEmpty()) {
        return;
      }
      String[] parts = requestLine.split(" ");
      if (parts.length < 2) {
        writeText(output, 400, "text/plain; charset=utf-8", "请求格式错误");
        return;
      }
      String method = parts[0];
      String target = parts[1];

      Map<String, String> headers = new HashMap<>();
      String line;
      int guard = 0;
      while ((line = readLine(rawInput)) != null && !line.isEmpty() && guard++ < MAX_HEADER_LINES) {
        int colon = line.indexOf(':');
        if (colon > 0) {
          headers.put(line.substring(0, colon).trim().toLowerCase(Locale.ROOT),
              line.substring(colon + 1).trim());
        }
      }

      String path = target;
      String query = "";
      int mark = target.indexOf('?');
      if (mark >= 0) {
        path = target.substring(0, mark);
        query = target.substring(mark + 1);
      }
      path = URLDecoder.decode(path, "UTF-8");

      if ("GET".equals(method) && (path.isEmpty() || "/".equals(path))) {
        serveIndex(output);
      } else if ("GET".equals(method) && "/health".equals(path)) {
        writeText(output, 200, "text/plain; charset=utf-8", "ok");
      } else if ("GET".equals(method) && path.startsWith("/f/")) {
        serveFile(output, headers, path.substring(3));
      } else if ("POST".equals(method) && "/upload".equals(path)) {
        receiveUpload(rawInput, output, headers, query);
      } else {
        writeText(output, 404, "text/plain; charset=utf-8", "没有这个地址");
      }
    } catch (IOException e) {
      logger.log("处理请求失败：" + e.getMessage());
    }
  }

  // ------------------------------------------------------------------ 文件列表

  private void serveIndex(OutputStream output) throws IOException {
    List<File> files = listFiles();
    long totalBytes = 0;
    for (File file : files) {
      totalBytes += file.length();
    }

    StringBuilder html = new StringBuilder();
    html.append("<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">")
        .append("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">")
        .append("<title>局域网快传</title><style>")
        .append("body{font-family:system-ui,-apple-system,'Microsoft YaHei',sans-serif;margin:0;padding:16px;")
        .append("background:#f5f6f8;color:#1f1f1f}")
        .append("h1{font-size:20px;margin:0 0 8px}h2{font-size:16px;margin:24px 0 8px}")
        .append(".hint{color:#666;font-size:13px;line-height:1.6;margin:0 0 16px}")
        .append("ul{list-style:none;padding:0;margin:0}")
        .append("li{background:#fff;border-radius:10px;padding:12px;margin-bottom:10px;")
        .append("display:flex;align-items:center;gap:10px}")
        .append(".name{flex:1;word-break:break-all;font-size:15px}")
        .append(".size{color:#999;font-size:12px;margin-left:6px}")
        .append("a.btn,button{background:#1677ff;color:#fff;border:0;border-radius:8px;padding:10px 16px;")
        .append("font-size:15px;text-decoration:none;display:inline-block;min-height:24px;cursor:pointer}")
        .append("input[type=file]{width:100%;font-size:15px;margin-bottom:10px}")
        .append("#bar{height:8px;background:#e5e5e5;border-radius:4px;overflow:hidden;margin-top:10px;display:none}")
        .append("#bar i{display:block;height:100%;width:0;background:#52c41a;transition:width .2s}")
        .append("</style></head><body>");
    html.append("<h1>局域网快传</h1>");
    html.append("<p class=\"hint\">你正在访问对方手机上的文件。同一 WiFi / 热点下才能打开；")
        .append("点「下载」就能保存到这台手机。</p>");

    html.append("<h2>可以下载的文件（").append(files.size()).append(" 个，共 ")
        .append(formatSize(totalBytes)).append("）</h2>");
    if (files.isEmpty()) {
      html.append("<p class=\"hint\">对方还没有放文件进来。</p>");
    } else {
      html.append("<ul>");
      for (File file : files) {
        html.append("<li><span class=\"name\">").append(escapeHtml(file.getName()))
            .append("<span class=\"size\">").append(formatSize(file.length())).append("</span></span>")
            .append("<a class=\"btn\" href=\"/f/").append(encodePath(file.getName())).append("\">下载</a></li>");
      }
      html.append("</ul>");
    }

    html.append("<h2>把文件发给对方</h2>")
        .append("<input type=\"file\" id=\"file\">")
        .append("<button onclick=\"upload()\">上传给对方</button>")
        .append("<div id=\"bar\"><i></i></div>")
        .append("<p class=\"hint\" id=\"msg\"></p>")
        .append("<script>")
        .append("function upload(){var f=document.getElementById('file').files[0];")
        .append("if(!f){document.getElementById('msg').textContent='请先选择一个文件';return;}")
        .append("var bar=document.getElementById('bar'),fill=bar.firstChild;bar.style.display='block';")
        .append("var xhr=new XMLHttpRequest();xhr.open('POST','/upload?name='+encodeURIComponent(f.name));")
        .append("xhr.upload.onprogress=function(e){if(e.lengthComputable){fill.style.width=(e.loaded*100/e.total)+'%';}};")
        .append("xhr.onload=function(){document.getElementById('msg').textContent=")
        .append("xhr.status===200?'上传完成，刷新页面就能看到':'上传失败（'+xhr.status+'）';};")
        .append("xhr.onerror=function(){document.getElementById('msg').textContent='上传失败：连接中断';};")
        .append("xhr.send(f);}")
        .append("</script></body></html>");

    writeText(output, 200, "text/html; charset=utf-8", html.toString());
  }

  // -------------------------------------------------------------------- 下载

  private void serveFile(OutputStream output, Map<String, String> headers, String rawName)
      throws IOException {
    File file = resolveShareFile(rawName);
    if (file == null || !file.isFile()) {
      writeText(output, 404, "text/plain; charset=utf-8", "文件不存在或已删除");
      return;
    }

    long length = file.length();
    long start = 0;
    long end = length - 1;
    boolean partial = false;
    String rangeHeader = headers.get("range");
    if (rangeHeader != null && rangeHeader.startsWith("bytes=")) {
      long[] parsed = parseRange(rangeHeader.substring("bytes=".length()), length);
      if (parsed == null) {
        StringBuilder head = new StringBuilder();
        head.append("HTTP/1.1 416 Range Not Satisfiable\r\n")
            .append("Content-Range: bytes */").append(length).append("\r\n")
            .append("Content-Length: 0\r\nConnection: close\r\n\r\n");
        output.write(head.toString().getBytes(StandardCharsets.UTF_8));
        output.flush();
        return;
      }
      start = parsed[0];
      end = parsed[1];
      partial = true;
    }

    long contentLength = end - start + 1;
    StringBuilder head = new StringBuilder();
    head.append(partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n")
        .append("Content-Type: application/octet-stream\r\n")
        .append("Accept-Ranges: bytes\r\n")
        .append("Content-Disposition: attachment; filename=\"")
        .append(asciiFallbackName(file.getName())).append("\"; filename*=UTF-8''")
        .append(encodePath(file.getName())).append("\r\n")
        .append("Content-Length: ").append(contentLength).append("\r\n")
        .append("Cache-Control: no-store\r\n")
        .append("Connection: close\r\n");
    if (partial) {
      head.append("Content-Range: bytes ").append(start).append('-').append(end).append('/')
          .append(length).append("\r\n");
    }
    head.append("\r\n");
    output.write(head.toString().getBytes(StandardCharsets.UTF_8));

    try (InputStream input = new FileInputStream(file)) {
      long skipped = 0;
      while (skipped < start) {
        long step = input.skip(start - skipped);
        if (step <= 0) {
          break;
        }
        skipped += step;
      }
      byte[] buffer = new byte[BUFFER_SIZE];
      long remaining = contentLength;
      while (remaining > 0) {
        int want = (int) Math.min(buffer.length, remaining);
        int read = input.read(buffer, 0, want);
        if (read < 0) {
          break;
        }
        output.write(buffer, 0, read);
        remaining -= read;
      }
      output.flush();
    }
  }

  /**
   * 解析 Range。返回 {start, end}（含端点）；不合法返回 null。
   * 支持 bytes=a-b、bytes=a-、bytes=-n 三种写法。
   */
  static long[] parseRange(String spec, long length) {
    if (length <= 0) {
      return null;
    }
    String value = spec.split(",")[0].trim();
    int dash = value.indexOf('-');
    if (dash < 0) {
      return null;
    }
    String left = value.substring(0, dash).trim();
    String right = value.substring(dash + 1).trim();
    long start;
    long end;
    try {
      if (left.isEmpty()) {
        // bytes=-n：最后 n 个字节
        long suffix = Long.parseLong(right);
        if (suffix <= 0) {
          return null;
        }
        start = Math.max(0, length - suffix);
        end = length - 1;
      } else {
        start = Long.parseLong(left);
        end = right.isEmpty() ? length - 1 : Long.parseLong(right);
      }
    } catch (NumberFormatException e) {
      return null;
    }
    if (start < 0 || start >= length || end < start) {
      return null;
    }
    end = Math.min(end, length - 1);
    return new long[] {start, end};
  }

  // -------------------------------------------------------------------- 上传

  private void receiveUpload(InputStream input, OutputStream output, Map<String, String> headers,
      String query) throws IOException {
    String name = firstQueryValue(query, "name");
    File target = resolveShareFile(name);
    if (target == null) {
      writeJson(output, 400, "{\"ok\":false,\"error\":\"文件名不合法\"}");
      return;
    }

    long declared = -1;
    String contentLength = headers.get("content-length");
    if (contentLength != null) {
      try {
        declared = Long.parseLong(contentLength.trim());
      } catch (NumberFormatException ignored) {
        declared = -1;
      }
    }
    if (declared < 0) {
      writeJson(output, 411, "{\"ok\":false,\"error\":\"缺少 Content-Length\"}");
      return;
    }
    if (declared > MAX_UPLOAD_BYTES) {
      writeJson(output, 413, "{\"ok\":false,\"error\":\"文件太大（上限 2 GB）\"}");
      return;
    }
    if (shareDir.getUsableSpace() < declared + 64L * 1024 * 1024) {
      writeJson(output, 507, "{\"ok\":false,\"error\":\"手机剩余空间不足\"}");
      return;
    }

    File temp = new File(shareDir, target.getName() + ".part");
    long written = 0;
    try (OutputStream fileOut = new BufferedOutputStream(new FileOutputStream(temp), BUFFER_SIZE)) {
      byte[] buffer = new byte[BUFFER_SIZE];
      while (written < declared) {
        int want = (int) Math.min(buffer.length, declared - written);
        int read = input.read(buffer, 0, want);
        if (read < 0) {
          break;
        }
        fileOut.write(buffer, 0, read);
        written += read;
      }
    }
    if (written != declared) {
      //noinspection ResultOfMethodCallIgnored
      temp.delete();
      writeJson(output, 400, "{\"ok\":false,\"error\":\"上传中断，请重试\"}");
      return;
    }
    if (target.exists() && !target.delete()) {
      logger.log("覆盖旧文件失败：" + target.getName());
    }
    if (!temp.renameTo(target)) {
      //noinspection ResultOfMethodCallIgnored
      temp.delete();
      writeJson(output, 500, "{\"ok\":false,\"error\":\"保存失败\"}");
      return;
    }
    logger.log("收到文件 | " + target.getName() + " | " + written + " 字节");
    writeJson(output, 200, "{\"ok\":true,\"name\":\"" + escapeJson(target.getName()) + "\"}");
  }

  // ------------------------------------------------------------------ 小工具

  /**
   * 把请求里的名字映射成分享目录内的文件；不合法（含路径分隔符、..、绝对路径、空名、
   * 解析后跑到目录外）一律返回 null。
   */
  File resolveShareFile(String rawName) {
    if (rawName == null) {
      return null;
    }
    String name = rawName.trim();
    if (name.isEmpty() || name.length() > 200) {
      return null;
    }
    if (name.contains("/") || name.contains("\\") || name.contains("..")) {
      return null;
    }
    if (name.startsWith(".") || name.indexOf('\0') >= 0) {
      return null;
    }
    File candidate = new File(shareDir, name);
    try {
      String dirPath = shareDir.getCanonicalPath() + File.separator;
      if (!candidate.getCanonicalPath().startsWith(dirPath)) {
        return null;
      }
    } catch (IOException e) {
      return null;
    }
    return candidate;
  }

  private static String readLine(InputStream input) throws IOException {
    StringBuilder line = new StringBuilder(96);
    int read;
    while ((read = input.read()) >= 0) {
      if (read == '\n') {
        break;
      }
      if (read != '\r') {
        line.append((char) read);
      }
      if (line.length() > 8192) {
        break;
      }
    }
    if (read < 0 && line.length() == 0) {
      return null;
    }
    return line.toString();
  }

  private static String firstQueryValue(String query, String key) {
    if (query == null || query.isEmpty()) {
      return null;
    }
    for (String pair : query.split("&")) {
      int eq = pair.indexOf('=');
      if (eq <= 0) {
        continue;
      }
      if (key.equals(pair.substring(0, eq))) {
        return URLDecoder.decode(pair.substring(eq + 1), StandardCharsets.UTF_8);
      }
    }
    return null;
  }

  static String formatSize(long bytes) {
    if (bytes < 1024) {
      return bytes + " B";
    }
    if (bytes < 1024 * 1024) {
      return String.format(Locale.ROOT, "%.1f KB", bytes / 1024.0);
    }
    if (bytes < 1024L * 1024 * 1024) {
      return String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024));
    }
    return String.format(Locale.ROOT, "%.2f GB", bytes / (1024.0 * 1024 * 1024));
  }

  static String escapeHtml(String text) {
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        .replace("\"", "&quot;");
  }

  static String escapeJson(String text) {
    return text.replace("\\", "\\\\").replace("\"", "\\\"");
  }

  static String encodePath(String name) {
    try {
      // URLEncoder 会把空格编码成 '+'，路径里必须是 %20
      return URLEncoder.encode(name, "UTF-8").replace("+", "%20");
    } catch (Exception e) {
      return name;
    }
  }

  /** Content-Disposition 的 ASCII 回退名：非 ASCII 字符统一换成 '_' */
  static String asciiFallbackName(String name) {
    StringBuilder builder = new StringBuilder(name.length());
    for (char ch : name.toCharArray()) {
      builder.append(ch < 128 && ch != '"' && ch != '\\' ? ch : '_');
    }
    return builder.toString();
  }

  private static void writeText(OutputStream output, int status, String contentType, String body)
      throws IOException {
    byte[] payload = body.getBytes(StandardCharsets.UTF_8);
    String head = "HTTP/1.1 " + status + " " + reason(status) + "\r\n"
        + "Content-Type: " + contentType + "\r\n"
        + "Content-Length: " + payload.length + "\r\n"
        + "Cache-Control: no-store\r\n"
        + "Connection: close\r\n\r\n";
    output.write(head.getBytes(StandardCharsets.UTF_8));
    output.write(payload);
    output.flush();
  }

  private static void writeJson(OutputStream output, int status, String body) throws IOException {
    writeText(output, status, "application/json; charset=utf-8", body);
  }

  private static String reason(int status) {
    switch (status) {
      case 200:
        return "OK";
      case 206:
        return "Partial Content";
      case 400:
        return "Bad Request";
      case 404:
        return "Not Found";
      case 411:
        return "Length Required";
      case 413:
        return "Payload Too Large";
      case 416:
        return "Range Not Satisfiable";
      case 500:
        return "Internal Server Error";
      case 507:
        return "Insufficient Storage";
      default:
        return "Error";
    }
  }

  private static void closeQuietly(java.io.Closeable closeable) {
    if (closeable == null) {
      return;
    }
    try {
      closeable.close();
    } catch (IOException ignored) {
      // 关闭失败无所谓
    }
  }

  /** 供测试断言用：当前允许上传的上限 */
  static long maxUploadBytes() {
    return MAX_UPLOAD_BYTES;
  }

  /** 便于诊断：列出支持的路由 */
  static List<String> routes() {
    return Arrays.asList("GET /", "GET /f/{name}", "POST /upload?name={name}", "GET /health");
  }
}
