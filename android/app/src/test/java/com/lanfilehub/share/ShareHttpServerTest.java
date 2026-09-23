package com.lanfilehub.share;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * 内置 HTTP 服务的真实测试（在电脑的 JVM 上跑，不需要手机）。
 *
 * <p>这是"没有真机也能验证"的关键：{@link ShareHttpServer} 不依赖任何 android.* 类，
 * 所以可以在这里起一个真服务、发真请求，把**下载、断点续传（Range）、上传、目录穿越防护**
 * 全部验一遍；Android 那一层只负责把服务启起来 + 显示二维码。
 */
public class ShareHttpServerTest {

  @Rule public TemporaryFolder folder = new TemporaryFolder();

  private ShareHttpServer server;
  private String baseUrl;

  @Before
  public void setUp() throws Exception {
    File shareDir = folder.newFolder("share");
    writeFile(new File(shareDir, "hello.txt"), "hello-lan-file-hub");
    writeFile(new File(shareDir, "中文 文件名.txt"), "chinese-name-content");
    server = new ShareHttpServer(shareDir, 0, message -> { });
    server.start();
    baseUrl = "http://127.0.0.1:" + server.getPort();
  }

  @After
  public void tearDown() {
    if (server != null) {
      server.stop();
    }
  }

  @Test
  public void healthEndpointAnswersOk() throws Exception {
    Response response = get("/health", null);
    assertEquals(200, response.status);
    assertEquals("ok", response.bodyAsString());
  }

  @Test
  public void indexListsSharedFiles() throws Exception {
    Response response = get("/", null);
    assertEquals(200, response.status);
    assertTrue(response.bodyAsString().contains("hello.txt"));
    assertTrue(response.bodyAsString().contains("局域网快传"));
    // 列表里必须给出发下载入口
    assertTrue(response.bodyAsString().contains("/f/hello.txt"));
  }

  @Test
  public void downloadsWholeFile() throws Exception {
    Response response = get("/f/hello.txt", null);
    assertEquals(200, response.status);
    assertEquals("hello-lan-file-hub", response.bodyAsString());
    assertEquals("bytes", response.header("Accept-Ranges"));
    assertTrue(response.header("Content-Disposition").contains("attachment"));
  }

  @Test
  public void supportsRangeForResumableDownload() throws Exception {
    Response response = get("/f/hello.txt", "bytes=0-4");
    assertEquals(206, response.status);
    assertEquals("hello", response.bodyAsString());
    assertEquals("bytes 0-4/18", response.header("Content-Range"));
  }

  @Test
  public void supportsSuffixRange() throws Exception {
    Response response = get("/f/hello.txt", "bytes=-6");
    assertEquals(206, response.status);
    assertEquals("hub", response.bodyAsString().substring(response.bodyAsString().length() - 3));
    assertEquals("bytes 12-17/18", response.header("Content-Range"));
  }

  @Test
  public void unsatisfiableRangeReturns416() throws Exception {
    Response response = get("/f/hello.txt", "bytes=999-1000");
    assertEquals(416, response.status);
    assertEquals("bytes */18", response.header("Content-Range"));
  }

  @Test
  public void missingFileReturns404() throws Exception {
    assertEquals(404, get("/f/nope.txt", null).status);
  }

  @Test
  public void chineseFileNameWorks() throws Exception {
    Response listing = get("/", null);
    assertTrue(listing.bodyAsString().contains("%E4%B8%AD%E6%96%87"));  // URL 编码后的中文名
    Response download = get("/f/" + ShareHttpServer.encodePath("中文 文件名.txt"), null);
    assertEquals(200, download.status);
    assertEquals("chinese-name-content", download.bodyAsString());
  }

  @Test
  public void refusesPathTraversal() throws Exception {
    // 三种常见穿越写法都必须被挡住（返回 404，且绝不能读到分享目录外的文件）
    assertEquals(404, get("/f/..%2F..%2Fsecret.txt", null).status);
    assertEquals(404, get("/f/%2e%2e%2f%2e%2e%2fsecret.txt", null).status);
    assertEquals(404, get("/f/..%5C..%5Csecret.txt", null).status);
    assertNotNull(server.resolveShareFile("hello.txt"));
    assertEquals(null, server.resolveShareFile("../secret.txt"));
    assertEquals(null, server.resolveShareFile("/etc/passwd"));
    assertEquals(null, server.resolveShareFile(".hidden"));
  }

  @Test
  public void uploadsFileFromBrowser() throws Exception {
    byte[] payload = "uploaded-from-friend-phone".getBytes(StandardCharsets.UTF_8);
    Response response = post("/upload?name=from-friend.txt", payload);
    assertEquals(200, response.status);
    assertTrue(response.bodyAsString().contains("\"ok\":true"));

    File saved = new File(server.getShareDir(), "from-friend.txt");
    assertTrue(saved.exists());
    assertEquals(payload.length, saved.length());
    assertEquals("uploaded-from-friend-phone", readFile(saved));
  }

  @Test
  public void uploadRejectsBadName() throws Exception {
    Response response = post("/upload?name=..%2Fevil.txt", "x".getBytes(StandardCharsets.UTF_8));
    assertEquals(400, response.status);
    assertFalse(new File(server.getShareDir().getParentFile(), "evil.txt").exists());
  }

  @Test
  public void uploadWithoutLengthIsRejected() throws Exception {
    // 浏览器 fetch 一定会带 Content-Length；这里用 chunked 模拟"没有长度"的坏请求
    URL url = new URL(baseUrl + "/upload?name=chunked.txt");
    HttpURLConnection connection = (HttpURLConnection) url.openConnection();
    connection.setRequestMethod("POST");
    connection.setDoOutput(true);
    connection.setChunkedStreamingMode(16);
    try (OutputStream out = connection.getOutputStream()) {
      out.write("chunked-body".getBytes(StandardCharsets.UTF_8));
    }
    assertEquals(411, connection.getResponseCode());
    connection.disconnect();
  }

  @Test
  public void rangeParsingHandlesEdgeCases() {
    assertEquals(null, ShareHttpServer.parseRange("0-4", 0));
    assertEquals(null, ShareHttpServer.parseRange("abc", 10));
    long[] head = ShareHttpServer.parseRange("0-4", 18);
    assertNotNull(head);
    assertEquals(0, head[0]);
    assertEquals(4, head[1]);
    long[] open = ShareHttpServer.parseRange("10-", 18);
    assertNotNull(open);
    assertEquals(17, open[1]);
    // 超过末尾要收敛到 length-1，而不是报错
    long[] clamped = ShareHttpServer.parseRange("10-999", 18);
    assertNotNull(clamped);
    assertEquals(17, clamped[1]);
  }

  @Test
  public void sizeFormattingIsReadable() {
    assertEquals("512 B", ShareHttpServer.formatSize(512));
    assertEquals("1.0 KB", ShareHttpServer.formatSize(1024));
    assertEquals("1.0 MB", ShareHttpServer.formatSize(1024 * 1024));
    assertEquals("1.00 GB", ShareHttpServer.formatSize(1024L * 1024 * 1024));
  }

  @Test
  public void fileListIsSortedByNewestFirst() throws Exception {
    File older = new File(server.getShareDir(), "older.txt");
    writeFile(older, "old");
    assertTrue(older.setLastModified(System.currentTimeMillis() - 60_000));
    File newer = new File(server.getShareDir(), "newer.txt");
    writeFile(newer, "new");
    assertTrue(newer.setLastModified(System.currentTimeMillis()));
    assertEquals("newer.txt", server.listFiles().get(0).getName());
  }

  // ------------------------------------------------------------------ 测试辅助

  private static void writeFile(File file, String content) throws Exception {
    try (OutputStream out = new FileOutputStream(file)) {
      out.write(content.getBytes(StandardCharsets.UTF_8));
    }
  }

  private static String readFile(File file) throws Exception {
    try (InputStream in = new java.io.FileInputStream(file)) {
      ByteArrayOutputStream buffer = new ByteArrayOutputStream();
      byte[] chunk = new byte[4096];
      int read;
      while ((read = in.read(chunk)) > 0) {
        buffer.write(chunk, 0, read);
      }
      return buffer.toString("UTF-8");
    }
  }

  private Response get(String path, String rangeHeader) throws Exception {
    HttpURLConnection connection = (HttpURLConnection) new URL(baseUrl + path).openConnection();
    connection.setRequestMethod("GET");
    if (rangeHeader != null) {
      connection.setRequestProperty("Range", rangeHeader);
    }
    return read(connection);
  }

  private Response post(String path, byte[] body) throws Exception {
    HttpURLConnection connection = (HttpURLConnection) new URL(baseUrl + path).openConnection();
    connection.setRequestMethod("POST");
    connection.setDoOutput(true);
    connection.setFixedLengthStreamingMode(body.length);
    try (OutputStream out = connection.getOutputStream()) {
      out.write(body);
    }
    return read(connection);
  }

  private static Response read(HttpURLConnection connection) throws Exception {
    int status = connection.getResponseCode();
    InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
    ByteArrayOutputStream buffer = new ByteArrayOutputStream();
    if (stream != null) {
      byte[] chunk = new byte[4096];
      int read;
      while ((read = stream.read(chunk)) > 0) {
        buffer.write(chunk, 0, read);
      }
      stream.close();
    }
    Response response = new Response();
    response.status = status;
    response.body = buffer.toByteArray();
    response.contentRange = connection.getHeaderField("Content-Range");
    response.acceptRanges = connection.getHeaderField("Accept-Ranges");
    response.contentDisposition = connection.getHeaderField("Content-Disposition");
    connection.disconnect();
    return response;
  }

  private static final class Response {
    int status;
    byte[] body;
    String contentRange;
    String acceptRanges;
    String contentDisposition;

    String bodyAsString() {
      return new String(body, StandardCharsets.UTF_8);
    }

    String header(String name) {
      switch (name) {
        case "Content-Range":
          return contentRange;
        case "Accept-Ranges":
          return acceptRanges;
        case "Content-Disposition":
          return contentDisposition;
        default:
          return null;
      }
    }
  }
}
