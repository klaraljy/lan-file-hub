package com.lanfilehub.share;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import org.junit.Test;

/**
 * 地址解析的单元测试（纯逻辑，不需要模拟器）。
 *
 * <p>用户会输"电脑上显示的那个地址"，可能带 http://、带斜杠、带端口，也可能只输 IP ——
 * 这里把这些写法都钉死，免得用户输进去却连不上。
 */
public class ServerStoreTest {

  @Test
  public void parsesBareHostWithDefaultPort() {
    ServerStore.Server server = ServerStore.parse("192.168.43.240");
    assertNotNull(server);
    assertEquals("192.168.43.240", server.host);
    assertEquals(9180, server.port);
    assertEquals("http://192.168.43.240:9180", server.baseUrl());
  }

  @Test
  public void parsesHostWithPort() {
    ServerStore.Server server = ServerStore.parse("10.0.0.5:9080");
    assertNotNull(server);
    assertEquals("10.0.0.5", server.host);
    assertEquals(9080, server.port);
  }

  @Test
  public void parsesFullUrlAndTrimsPath() {
    ServerStore.Server server = ServerStore.parse("  http://10.0.0.5:9180/files/abc  ");
    assertNotNull(server);
    assertEquals("10.0.0.5", server.host);
    assertEquals(9180, server.port);
    assertEquals("http://10.0.0.5:9180", server.baseUrl());
  }

  @Test
  public void parsesHttpsUrlAsPlainHttpOnLan() {
    // 局域网没有证书；用户从别处复制来 https 也要能用（按 http 连）
    ServerStore.Server server = ServerStore.parse("https://192.168.1.9/");
    assertNotNull(server);
    assertEquals("192.168.1.9", server.host);
    assertEquals(9180, server.port);
  }

  @Test
  public void rejectsGarbage() {
    assertNull(ServerStore.parse(null));
    assertNull(ServerStore.parse(""));
    assertNull(ServerStore.parse("   "));
    assertNull(ServerStore.parse("192.168.1.5:notaport"));
    assertNull(ServerStore.parse("192.168.1.5:70000"));
    assertNull(ServerStore.parse("has space"));
  }

  @Test
  public void displayNameFallsBackToHost() {
    ServerStore.Server server = ServerStore.parse("192.168.1.7");
    assertNotNull(server);
    assertEquals("192.168.1.7", server.displayName());
    server.label = "客厅的电脑";
    assertEquals("客厅的电脑", server.displayName());
  }
}
