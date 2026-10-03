package com.lanfilehub.share;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.After;
import org.junit.Test;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * App 内「检查更新」（用户 2026-10-03 批准）的纯逻辑测试。
 *
 * <p>钉两件事：
 *   ① **版本怎么比** —— 比错了要么让用户白下载一遍，要么明明有新版却说"已是最新"；
 *   ② **后端那句话怎么读** —— 字段名是后端定的，读错了界面上就是一堆空。
 *
 * <p>用一个真的小 HTTP 服务（回环随机端口，见 {@link #serve}）把 {@link AppUpdate#check}
 * 整条链路跑一遍："读接口 → 判版本 → 给出人话"这套逻辑能在电脑上验，就不该等到手机上才发现。
 * ⛔ 不用 {@code com.sun.net.httpserver}：Android 单测的编译 classpath 里没有它（实测报"程序包不存在"），
 * 只有 JDK 的标准 Socket API 才两处都在。
 */
public class AppUpdateTest {

  /** 后端真实的响应形状（见 backend/src/api/app_release_controller.cpp） */
  private static final String RELEASE_BODY =
      "{\"data\":{\"available\":true,\"file_name\":\"lan-file-hub.apk\",\"version_name\":\"1.0\","
          + "\"version_code\":5,\"size_bytes\":372736,\"built_at\":\"2026-10-03T10:40:11Z\","
          + "\"sha256\":\"abc123\",\"download_path\":\"/api/v1/app/download\"},"
          + "\"request_id\":\"req-1\"}";

  private ServerSocket socket;
  private Thread serverThread;
  private final AtomicInteger requests = new AtomicInteger();

  @After
  public void stopServer() throws IOException {
    if (socket != null) {
      socket.close();
      socket = null;
    }
    if (serverThread != null) {
      serverThread.interrupt();
      serverThread = null;
    }
  }

  /**
   * 起一个只在回环上的小服务：按固定状态码 + 固定响应体回答**一次**请求，返回它的端口。
   * 用裸 {@link ServerSocket}（HTTP/1.1 足够简单：读掉请求头 → 写响应 → 关连接）。
   */
  private int serve(int status, String body) throws IOException {
    socket = new ServerSocket(0, 4, java.net.InetAddress.getByName("127.0.0.1"));
    final ServerSocket listening = socket;
    final byte[] payload = body.getBytes(StandardCharsets.UTF_8);
    final String header = "HTTP/1.1 " + status + " " + (status == 200 ? "OK" : "Error") + "\r\n"
        + "Content-Type: application/json\r\n"
        + "Content-Length: " + payload.length + "\r\n"
        + "Connection: close\r\n\r\n";
    serverThread = new Thread(() -> {
      try (Socket client = listening.accept()) {
        requests.incrementAndGet();
        BufferedReader reader =
            new BufferedReader(new InputStreamReader(client.getInputStream(), StandardCharsets.UTF_8));
        String line;
        while ((line = reader.readLine()) != null && !line.isEmpty()) {
          // 把请求头读干净再回答（读到空行 = 请求头结束）
        }
        OutputStream out = client.getOutputStream();
        out.write(header.getBytes(StandardCharsets.UTF_8));
        out.write(payload);
        out.flush();
      } catch (IOException ignored) {
        // 测试收尾时把服务关掉会让 accept 抛异常，不关心
      }
    });
    serverThread.setDaemon(true);
    serverThread.start();
    return listening.getLocalPort();
  }

  // ---- 解析 -------------------------------------------------------------------

  @Test
  public void readsEveryFieldOfTheReleaseResponse() {
    AppUpdate.Release release = AppUpdate.parse(RELEASE_BODY);

    assertTrue(release.available);
    assertEquals("lan-file-hub.apk", release.fileName);
    assertEquals("1.0", release.versionName);
    assertEquals(5, release.versionCode);
    assertEquals(372736L, release.sizeBytes);
    assertEquals("2026-10-03T10:40:11Z", release.builtAt);
    assertEquals("/api/v1/app/download", release.downloadPath);
  }

  @Test
  public void missingFieldsFallBackInsteadOfBlowingUp() {
    // 后端没有安装包时 available=false、版本号为 0（第一次跑 build-android.ps1 之前就是这样）
    AppUpdate.Release release =
        AppUpdate.parse("{\"data\":{\"available\":false,\"version_code\":0},\"request_id\":\"r\"}");

    assertFalse(release.available);
    assertEquals(0, release.versionCode);
    assertEquals("", release.versionName);
    // 没有 download_path 也要能拼出下载地址（默认站内路径）
    assertEquals("/api/v1/app/download", release.downloadPath);
  }

  @Test
  public void garbageResponseMeansNoRelease() {
    assertFalse(AppUpdate.parse("").available);
    assertFalse(AppUpdate.parse("not json at all").available);
    assertFalse(AppUpdate.parse("{\"request_id\":\"r\"}").available);
    assertFalse(AppUpdate.parse(null).available);
    assertNull(AppUpdate.firstObject("{\"data\":\"不是对象\"}", "data"));
  }

  @Test
  public void versionNameWithBracesAndEscapesDoesNotConfuseTheParser() {
    // 名字里带花括号/引号也不能把"取 data 那一段"弄错（花括号计数要跳过字符串内部）
    AppUpdate.Release release =
        AppUpdate.parse(
            "{\"data\":{\"available\":true,\"version_name\":\"a{b\\\"c\",\"version_code\":7}}");

    assertTrue(release.available);
    assertEquals("a{b\"c", release.versionName);
    assertEquals(7, release.versionCode);
  }

  @Test
  public void parsesTheShapeWhereBooleanIsWrittenAsTrue() {
    AppUpdate.Release release = AppUpdate.parse("{\"data\":{\"available\":true,\"size_bytes\":1024}}");
    assertTrue(release.available);
    assertEquals(1024L, release.sizeBytes);
  }

  @Test
  public void downloadUrlAppendsThePathToTheServerAddress() {
    AppUpdate.Release release = AppUpdate.parse(RELEASE_BODY);
    assertEquals("http://10.200.18.61:9180/api/v1/app/download",
        release.downloadUrl("http://10.200.18.61:9180"));

    // 后端哪天改成给整条 URL，也要原样用
    AppUpdate.Release absolute =
        AppUpdate.parse("{\"data\":{\"download_path\":\"http://a.b:1/x.apk\"}}");
    assertEquals("http://a.b:1/x.apk", absolute.downloadUrl("http://10.0.0.1:9180"));
  }

  // ---- 版本比较 ---------------------------------------------------------------

  @Test
  public void newerBuildNumberMeansThereIsAnUpdate() {
    assertTrue(AppUpdate.hasNewerBuild(4, 5));
    assertTrue(AppUpdate.hasNewerBuild(1, 99));
  }

  @Test
  public void sameOrOlderBuildNumberMeansAlreadyLatest() {
    assertFalse(AppUpdate.hasNewerBuild(5, 5));
    assertFalse(AppUpdate.hasNewerBuild(9, 5));
  }

  @Test
  public void unknownServerBuildNeverTriggersADownload() {
    // 读不到服务器版本号（0）时不许说"有新版"——那会把用户引去下一个空包
    assertFalse(AppUpdate.hasNewerBuild(5, 0));
    assertFalse(AppUpdate.hasNewerBuild(5, -1));
  }

  // ---- 给人看的格式 -----------------------------------------------------------

  @Test
  public void sizesAreReadable() {
    assertEquals("", AppUpdate.humanSize(0));
    assertEquals("", AppUpdate.humanSize(-5));
    assertEquals("1 KB", AppUpdate.humanSize(1024));
    assertEquals("364 KB", AppUpdate.humanSize(372736));
    assertEquals("1.5 MB", AppUpdate.humanSize(1572864));
  }

  @Test
  public void buildTimeIsShownInLocalTime() {
    // 后端给 UTC，用户看的是本地时间：同一时刻换算过来，只有"时区偏移"会让小时数不同，
    // 所以钉"那一天 + 格式"，不钉具体小时（开发机时区变了也不该红）。
    String shown = AppUpdate.humanTime("2026-10-03T10:40:11Z");

    assertTrue(shown, shown.matches("\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}"));
    assertEquals("2026-10-03", shown.substring(0, 10));

    // 与本机时区自己算一遍对照，确认就是"UTC → 本地"这一次换算
    Date moment = null;
    try {
      SimpleDateFormat parser = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.US);
      parser.setTimeZone(TimeZone.getTimeZone("UTC"));
      moment = parser.parse("2026-10-03T10:40:11");
    } catch (java.text.ParseException error) {
      assertTrue("测试自己的时间串解析不了：" + error, false);
    }
    assertEquals(new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US).format(moment), shown);
  }

  @Test
  public void unreadableTimeIsShownAsIs() {
    assertEquals("", AppUpdate.humanTime(""));
    assertEquals("", AppUpdate.humanTime(null));
    assertEquals("2026/10/03", AppUpdate.humanTime("2026/10/03"));
  }

  @Test
  public void describeListsTheNumbersTheUserCaresAbout() {
    String text = AppUpdate.parse(RELEASE_BODY).describe();
    assertTrue(text, text.contains("版本 1.0"));
    assertTrue(text, text.contains("构建号 5"));
    assertTrue(text, text.contains("364 KB"));
  }

  @Test
  public void hostOfDropsSchemeAndPath() {
    assertEquals("10.200.18.61:9180", AppUpdate.hostOf("http://10.200.18.61:9180/"));
    assertEquals("10.200.18.61:9180", AppUpdate.hostOf("http://10.200.18.61:9180/api/v1/app/release"));
    assertEquals("", AppUpdate.hostOf(null));
  }

  // ---- 整条链路（真服务 + 真请求）----------------------------------------------

  @Test
  public void checkSaysUpdateAvailableWhenServerHasANewerBuild() throws Exception {
    int port = serve(200, RELEASE_BODY);
    AppUpdate.CheckResult result =
        AppUpdate.check(new ServerStore.Server("127.0.0.1", port, "", 0), 4);

    assertTrue(result.ok);
    assertTrue(result.updateAvailable);
    assertNotNull(result.release);
    assertEquals(5, result.release.versionCode);
    assertTrue(result.message, result.message.contains("构建号 5"));
    assertEquals("确实发了一次 HTTP 请求", 1, requests.get());
  }

  @Test
  public void checkSaysLatestWhenTheInstalledBuildIsTheSame() throws Exception {
    int port = serve(200, RELEASE_BODY);
    AppUpdate.CheckResult result =
        AppUpdate.check(new ServerStore.Server("127.0.0.1", port, "", 0), 5);

    assertTrue(result.ok);
    assertFalse(result.updateAvailable);
    assertTrue(result.message, result.message.contains("已是最新"));
  }

  @Test
  public void checkReportsNoPackageWhenTheServerHasNone() throws Exception {
    int port = serve(200, "{\"data\":{\"available\":false,\"version_code\":0},\"request_id\":\"r\"}");
    AppUpdate.CheckResult result =
        AppUpdate.check(new ServerStore.Server("127.0.0.1", port, "", 0), 4);

    assertFalse(result.ok);
    assertFalse(result.updateAvailable);
    assertTrue(result.message, result.message.contains("还没有安装包"));
  }

  @Test
  public void checkTurnsAnHttpErrorIntoPlainChinese() throws Exception {
    // 后端返回 500 时也不能把 HTTP 码丢给用户看
    int port = serve(500, "{\"error\":{\"code\":\"INTERNAL\"}}");
    AppUpdate.CheckResult result =
        AppUpdate.check(new ServerStore.Server("127.0.0.1", port, "", 0), 4);

    assertFalse(result.ok);
    assertFalse(result.updateAvailable);
    assertTrue(result.message, result.message.contains("连不上"));
  }

  @Test
  public void checkTurnsNetworkFailureIntoPlainChinese() {
    // 127.0.0.1:1 上没有人听 → 必须是一句人话，不能抛异常、不能显示 HTTP 码
    AppUpdate.CheckResult result =
        AppUpdate.check(new ServerStore.Server("127.0.0.1", 1, "", 0), 4);

    assertFalse(result.ok);
    assertFalse(result.updateAvailable);
    assertTrue(result.message, result.message.contains("连不上"));
  }

  @Test
  public void checkWithoutAnyKnownComputerTellsTheUserWhatToDoFirst() {
    AppUpdate.CheckResult result = AppUpdate.check(null, 4);

    assertFalse(result.ok);
    assertTrue(result.message, result.message.contains("找电脑"));
  }
}
