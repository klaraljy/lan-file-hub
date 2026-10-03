package com.lanfilehub.share;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * 当前网络判定（用户 2026-10-03：App 里要能一眼看出"连上没有"）。
 *
 * <p>只钉纯函数部分 —— 真机上的 ConnectivityManager 调用由 Android 提供，不在单测里。
 */
public class NetworkStateTest {

  @Test
  public void wifiWithGatewayAddressMeansPhoneHotspot() {
    assertEquals(NetworkState.Kind.HOTSPOT,
        NetworkState.classify(true, false, false, true));
    assertEquals(NetworkState.Kind.WIFI,
        NetworkState.classify(true, false, false, false));
  }

  @Test
  public void ethernetWinsOverEverythingElse() {
    assertEquals(NetworkState.Kind.ETHERNET,
        NetworkState.classify(true, true, true, true));
  }

  @Test
  public void cellularIsItsOwnKindAndCannotReachTheComputer() {
    NetworkState.Kind cellular = NetworkState.classify(false, false, true, false);
    assertEquals(NetworkState.Kind.CELLULAR, cellular);
    assertFalse("流量下连不到同一网络里的电脑", NetworkState.canReachComputer(cellular));

    assertTrue(NetworkState.canReachComputer(NetworkState.Kind.WIFI));
    assertTrue(NetworkState.canReachComputer(NetworkState.Kind.HOTSPOT));
    assertTrue(NetworkState.canReachComputer(NetworkState.Kind.ETHERNET));
    assertFalse(NetworkState.canReachComputer(NetworkState.Kind.NONE));
  }

  @Test
  public void hotspotGatewayAddressesAreRecognised() {
    assertTrue(NetworkState.looksLikeHotspotGateway("192.168.43.1"));
    assertTrue(NetworkState.looksLikeHotspotGateway("192.168.42.1"));
    assertTrue(NetworkState.looksLikeHotspotGateway("172.20.10.1"));
    // 同一网段但不是网关（是别人的热点，自己是客户端）→ 不是"我在开热点"
    assertFalse(NetworkState.looksLikeHotspotGateway("192.168.43.240"));
    assertFalse(NetworkState.looksLikeHotspotGateway("192.168.1.5"));
    assertFalse(NetworkState.looksLikeHotspotGateway(null));
  }
}
