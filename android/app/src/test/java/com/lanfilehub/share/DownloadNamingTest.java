package com.lanfilehub.share;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

/**
 * App 内下载的文件名解析（2026-10-03）。
 *
 * <p>为什么要单测：这些名字会被塞进手机的"下载"目录 —— 如果服务器返回的文件名里带着
 * {@code ../} 或 {@code \}，那就是路径穿越，能覆盖到别的目录。
 * 同时"下完叫什么名字"也是用户能看见的东西，猜错了很难看。
 */
public class DownloadNamingTest {

  @Test
  public void readsQuotedFileName() {
    assertEquals("lan-file-hub.apk",
        DownloadNaming.fromContentDisposition("attachment; filename=\"lan-file-hub.apk\""));
  }

  @Test
  public void readsUnquotedFileName() {
    assertEquals("1.mp4",
        DownloadNaming.fromContentDisposition("attachment; filename=1.mp4"));
  }

  @Test
  public void utf8FileNameIsKept() {
    assertEquals("同欢11.mp4",
        DownloadNaming.fromContentDisposition("attachment; filename=\"同欢11.mp4\""));
  }

  @Test
  public void missingHeaderMeansNull() {
    assertNull(DownloadNaming.fromContentDisposition(null));
    assertNull(DownloadNaming.fromContentDisposition(""));
    assertNull(DownloadNaming.fromContentDisposition("attachment"));
  }

  @Test
  public void pathSeparatorsAreReplaced() {
    // 服务器要是发了这种名字，绝不能让它变成路径
    assertEquals(".._.._windows_system32_evil.exe",
        DownloadNaming.fromContentDisposition("attachment; filename=\"../../windows/system32/evil.exe\""));
    assertEquals("a_b.mp4", DownloadNaming.sanitize("a\\b.mp4"));
  }

  @Test
  public void reservedNamesAreRejected() {
    assertNull(DownloadNaming.sanitize("."));
    assertNull(DownloadNaming.sanitize(".."));
    assertNull(DownloadNaming.sanitize("   "));
    assertNull(DownloadNaming.sanitize(null));
  }

  @Test
  public void urlFallbackDropsQueryAndKeepsLastSegment() {
    assertEquals("download",
        DownloadNaming.fromUrl("http://10.200.18.61:9180/api/v1/files/abc/download?ticket=xyz"));
    assertEquals("lan-file-hub.apk",
        DownloadNaming.fromUrl("http://10.200.18.61:9180/downloads/lan-file-hub.apk"));
  }

  @Test
  public void urlFallbackNeverReturnsEmpty() {
    assertEquals("download", DownloadNaming.fromUrl(""));
    assertEquals("download", DownloadNaming.fromUrl(null));
    assertEquals("download", DownloadNaming.fromUrl("http://host/"));
  }
}
