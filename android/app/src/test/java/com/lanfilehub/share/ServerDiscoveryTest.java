package com.lanfilehub.share;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.List;

/**
 * 自动找电脑（用户 2026-10-03 要求"点开 App 就该自动检测连上没有"）的纯逻辑测试。
 *
 * <p>这里只钉"算得对不对"：候选地址怎么算、怎么认出自家后端、热点网关怎么判。
 * 真的去扫网段要联网，放在手机上跑，不放进单测。
 */
public class ServerDiscoveryTest {

  @Test
  public void candidatesForSlash24SkipsNetworkBroadcastAndSelf() {
    List<String> hosts = ServerDiscovery.candidates("192.168.43.240", 24);

    assertEquals(253, hosts.size());  // .1 ~ .254 里去掉自己
    assertEquals("192.168.43.1", hosts.get(0));
    assertEquals("192.168.43.254", hosts.get(hosts.size() - 1));
    assertFalse("自己的地址不该出现在候选里", hosts.contains("192.168.43.240"));
    assertFalse("网络地址 .0 不该被探", hosts.contains("192.168.43.0"));
    assertFalse("广播地址 .255 不该被探", hosts.contains("192.168.43.255"));
  }

  @Test
  public void candidatesForOtherPrefixesStayInsideTheSubnet() {
    // /25：只扫前半段
    List<String> half = ServerDiscovery.candidates("192.168.1.10", 25);
    assertEquals(125, half.size());
    assertTrue(half.contains("192.168.1.1"));
    assertFalse(half.contains("192.168.1.200"));

    // /16：范围太大，必须截断（否则一次扫 6 万多个地址）
    List<String> big = ServerDiscovery.candidates("10.0.0.5", 16);
    assertEquals(512, big.size());
    assertEquals("10.0.0.1", big.get(0));
  }

  @Test
  public void candidatesAreEmptyWhenAddressIsUnusable() {
    assertTrue(ServerDiscovery.candidates(null, 24).isEmpty());
    assertTrue(ServerDiscovery.candidates("", 24).isEmpty());
    assertTrue(ServerDiscovery.candidates("不是地址", 24).isEmpty());
    assertTrue(ServerDiscovery.candidates("1.2.3", 24).isEmpty());
    assertTrue(ServerDiscovery.candidates("1.2.3.999", 24).isEmpty());
  }

  @Test
  public void addressConversionRoundTrips() {
    assertEquals(3232246768L, ServerDiscovery.ipToLong("192.168.43.240"));
    assertEquals("192.168.43.240", ServerDiscovery.longToIp(3232246768L));
    assertEquals("0.0.0.0", ServerDiscovery.longToIp(0L));
    assertEquals("255.255.255.255", ServerDiscovery.longToIp(4294967295L));
  }

  @Test
  public void recognisesOwnBackendByHealthPayload() {
    assertTrue(ServerDiscovery.looksLikeLanFileHub(
        "{\"status\":\"ok\",\"service\":\"lan-file-hub\",\"check\":\"live\"}"));
    // 别人的 80 端口回了网页、或者回了别的服务，都不能当成自家电脑
    assertFalse(ServerDiscovery.looksLikeLanFileHub("<html><body>router</body></html>"));
    assertFalse(ServerDiscovery.looksLikeLanFileHub("{\"status\":\"ok\",\"service\":\"other\"}"));
    assertFalse(ServerDiscovery.looksLikeLanFileHub(null));
    assertFalse(ServerDiscovery.looksLikeLanFileHub(""));
  }
}
