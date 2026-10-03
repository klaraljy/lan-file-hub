package com.lanfilehub.share;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * 网段宽度判定（2026-10-03 用户实测：校园网上电脑能连，但 App「找电脑」找不到）。
 *
 * <p>自动发现只扫本机那一段的 254 个地址；校园网常见 /16（六万多个地址），
 * 手机和电脑很可能不在同一 /24 —— 这时候必须给出"手动填地址"的提示，
 * <b>不能</b>一律说"这个 WiFi 开了客户端隔离"（那会把用户骗去开热点，AGENTS §14.1）。
 */
public class NetUtilTest {

  @Test
  public void twentyFourIsNotWide() {
    assertFalse("家用路由器/热点都是 /24，扫描能覆盖", NetUtil.isWideSubnet(24));
  }

  @Test
  public void campusStyleSubnetsAreWide() {
    // 校园网实测：10.200.18.61/16
    assertTrue(NetUtil.isWideSubnet(16));
    // 有些校园网/运营商用 /22、/20
    assertTrue(NetUtil.isWideSubnet(22));
    assertTrue(NetUtil.isWideSubnet(20));
  }

  @Test
  public void narrowerThanTwentyFourIsNotWide() {
    assertFalse(NetUtil.isWideSubnet(25));
    assertFalse(NetUtil.isWideSubnet(28));
  }

  @Test
  public void nonsensePrefixDoesNotCountAsWide() {
    // 拿不到掩码时按 24 处理（见 NetUtil.prefixLength），不能因此弹出"大网段"提示
    assertFalse(NetUtil.isWideSubnet(0));
    assertFalse(NetUtil.isWideSubnet(-1));
  }
}
