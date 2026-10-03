package com.lanfilehub.share;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

/**
 * 自动找电脑（用户 2026-10-03 要求："点开 App 应该能自动检测连上没有"）。
 *
 * <p>为什么要扫网段：手机和电脑在同一个 WiFi/热点里时，电脑的地址是路由器随机分的
 * （192.168.x.y），用户不可能知道，电脑换网络还会变。与其让用户手输或从旧记录里猜，
 * 不如把**自己所在网段的 254 个地址并发探一遍**，谁回 `/health/live` 且响应里写着
 * `"service":"lan-file-hub"` 就是自家电脑（后端早就带这个字段，不用改后端）。
 *
 * <p>代价与边界：
 *   - 只在 App 打开、且**没有已知地址在线**时才扫，避免每次都扫（省电）；
 *   - 每个地址连接/读取各 0.6 秒超时，32 个线程并发 → 典型 2~5 秒出结果；
 *   - 校园网这类开了**客户端隔离**的网络会一个都扫不到，这时给用户的提示必须是
 *     "改用手机热点"（AGENTS.md §14.6），而不是一句"没找到"。
 */
public final class ServerDiscovery {

  private static final int DEFAULT_TIMEOUT_MS = 600;
  private static final int DEFAULT_THREADS = 32;
  /** 一次最多探多少个地址：/24 是 254，再多就没意义了（也避免误扫大网段） */
  private static final int MAX_CANDIDATES = 512;

  private ServerDiscovery() {
  }

  /** 扫到的一台电脑 */
  public static final class Found {
    public final String host;
    public final int port;

    public Found(String host, int port) {
      this.host = host;
      this.port = port;
    }

    public String displayAddress() {
      return host + ":" + port;
    }

    @Override
    public boolean equals(Object other) {
      if (!(other instanceof Found)) {
        return false;
      }
      Found that = (Found) other;
      return port == that.port && host.equals(that.host);
    }

    @Override
    public int hashCode() {
      return host.hashCode() * 31 + port;
    }
  }

  /** 进度回调（都在后台线程上调用，界面更新请自己 post 回主线程） */
  public interface Progress {
    void onProgress(int done, int total);
  }

  /**
   * 由自己的地址与掩码长度算出同网段的候选地址（纯函数，单测覆盖）。
   *
   * <p>排除自己、排除网络地址（.0）与广播地址（.255）；前缀不是 /24 时按实际范围算，
   * 但最多 {@link #MAX_CANDIDATES} 个。拿不到掩码时按 /24 处理（家用/热点几乎都是 /24）。
   */
  public static List<String> candidates(String ownIp, int prefixLength) {
    List<String> result = new ArrayList<>();
    long own = ipToLong(ownIp);
    if (own < 0) {
      return result;
    }
    int prefix = prefixLength >= 8 && prefixLength <= 30 ? prefixLength : 24;
    long mask = prefix == 0 ? 0L : (0xFFFFFFFFL << (32 - prefix)) & 0xFFFFFFFFL;
    long network = own & mask;
    long broadcast = network | (~mask & 0xFFFFFFFFL);
    for (long value = network + 1; value < broadcast && result.size() < MAX_CANDIDATES; value++) {
      if (value == own) {
        continue;  // 自己不用探
      }
      result.add(longToIp(value));
    }
    return result;
  }

  /** 自己的 IPv4 与掩码长度；拿不到时返回 /24 的默认值。 */
  public static int prefixLengthOf(String ownIp) {
    try {
      InetAddress address = InetAddress.getByName(ownIp);
      java.net.NetworkInterface nif = java.net.NetworkInterface.getByInetAddress(address);
      if (nif != null) {
        for (java.net.InterfaceAddress entry : nif.getInterfaceAddresses()) {
          if (entry.getAddress() instanceof Inet4Address
              && entry.getAddress().getHostAddress().equals(ownIp)) {
            int length = entry.getNetworkPrefixLength();
            if (length > 0) {
              return length;
            }
          }
        }
      }
    } catch (Exception ignored) {
      // 落到默认值：家用与手机热点几乎都是 /24
    }
    return 24;
  }

  /** 响应体是不是自家后端（/health/live 里带 "service":"lan-file-hub"）。 */
  public static boolean looksLikeLanFileHub(String body) {
    return body != null && body.contains("\"service\"") && body.contains("lan-file-hub");
  }

  /** 探一个地址：是自家后端就返回 true。 */
  public static boolean probe(String host, int port, int timeoutMs) {
    HttpURLConnection connection = null;
    try {
      URL url = new URL("http://" + host + ":" + port + "/health/live");
      connection = (HttpURLConnection) url.openConnection();
      connection.setRequestMethod("GET");
      connection.setConnectTimeout(timeoutMs);
      connection.setReadTimeout(timeoutMs);
      connection.setUseCaches(false);
      if (connection.getResponseCode() != 200) {
        return false;
      }
      return looksLikeLanFileHub(readBody(connection.getInputStream()));
    } catch (IOException error) {
      return false;
    } finally {
      if (connection != null) {
        connection.disconnect();
      }
    }
  }

  /** 扫一遍当前网段。返回**排序后的**结果（地址小的在前，界面顺序稳定）。 */
  public static List<Found> scan(String ownIp, int port, Progress progress) {
    return scan(ownIp, prefixLengthOf(ownIp), new int[] {port}, DEFAULT_TIMEOUT_MS, DEFAULT_THREADS,
        progress);
  }

  /**
   * 要扫哪些端口：**永远先扫便携包默认端口**，再加上历史记录里用过的端口（去重、最多 2 个）。
   *
   * <p>⛔ 2026-10-03 踩过的坑：原来只扫"历史记录里第一条的端口"，用户换过端口（比如早期用
   * 5173 开发端口连过）时就会去扫一个根本没在听的端口 → 永远找不到电脑。默认端口必须无条件包含。
   */
  public static int[] portsToScan(int historyPort) {
    if (historyPort <= 0 || historyPort == DEFAULT_PORT_VALUE) {
      return new int[] {DEFAULT_PORT_VALUE};
    }
    return new int[] {DEFAULT_PORT_VALUE, historyPort};
  }

  /** 便携包默认端口（后端与网页同一个端口） */
  public static final int DEFAULT_PORT_VALUE = 9180;

  /** 多端口 + 默认超时/线程数（界面用的就是这个） */
  public static List<Found> scan(String ownIp, int prefixLength, int[] ports, Progress progress) {
    return scan(ownIp, prefixLength, ports, DEFAULT_TIMEOUT_MS, DEFAULT_THREADS, progress);
  }

  /** 供单测/调参用的完整版本（单端口）。 */
  public static List<Found> scan(String ownIp, int prefixLength, int port, int timeoutMs,
                                 int threads, Progress progress) {
    return scan(ownIp, prefixLength, new int[] {port}, timeoutMs, threads, progress);
  }

  /** 完整版本：多个端口各扫一遍，结果合并去重。 */
  public static List<Found> scan(String ownIp, int prefixLength, int[] ports, int timeoutMs,
                                 int threads, Progress progress) {
    List<String> hosts = candidates(ownIp, prefixLength);
    List<Found> found = Collections.synchronizedList(new ArrayList<Found>());
    if (hosts.isEmpty() || ports == null || ports.length == 0) {
      return found;
    }

    final int total = hosts.size() * ports.length;
    final int[] done = {0};
    ExecutorService pool = Executors.newFixedThreadPool(Math.max(1, threads));
    List<Future<?>> tasks = new ArrayList<>();
    for (final int port : ports) {
      for (final String host : hosts) {
        tasks.add(pool.submit(new Callable<Void>() {
          @Override
          public Void call() {
            if (probe(host, port, timeoutMs)) {
              found.add(new Found(host, port));
            }
            int finished;
            synchronized (done) {
              done[0]++;
              finished = done[0];
            }
            if (progress != null) {
              progress.onProgress(finished, total);
            }
            return null;
          }
        }));
      }
    }

    pool.shutdown();
    try {
      pool.awaitTermination((long) (total / Math.max(1, threads) + 2) * timeoutMs + 2000L,
          TimeUnit.MILLISECONDS);
      for (Future<?> task : tasks) {
        task.get();
      }
    } catch (InterruptedException interrupted) {
      Thread.currentThread().interrupt();
    } catch (ExecutionException ignored) {
      // 单个地址探测异常不影响其它地址
    }

    List<Found> sorted = new ArrayList<>(found);
    Collections.sort(sorted, (left, right) -> {
      long a = ipToLong(left.host);
      long b = ipToLong(right.host);
      int byAddress = Long.compare(a, b);
      return byAddress != 0 ? byAddress : Integer.compare(left.port, right.port);
    });
    return sorted;
  }

  // ---- 地址换算（纯函数，单测覆盖）--------------------------------------------

  public static long ipToLong(String ip) {
    if (ip == null) {
      return -1;
    }
    String[] parts = ip.split("\\.");
    if (parts.length != 4) {
      return -1;
    }
    long value = 0;
    for (String part : parts) {
      int piece;
      try {
        piece = Integer.parseInt(part);
      } catch (NumberFormatException error) {
        return -1;
      }
      if (piece < 0 || piece > 255) {
        return -1;
      }
      value = (value << 8) | piece;
    }
    return value;
  }

  public static String longToIp(long value) {
    return ((value >> 24) & 0xFF) + "." + ((value >> 16) & 0xFF) + "." + ((value >> 8) & 0xFF) + "."
        + (value & 0xFF);
  }

  private static String readBody(InputStream stream) throws IOException {
    StringBuilder builder = new StringBuilder();
    try (BufferedReader reader =
             new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))) {
      String line;
      while ((line = reader.readLine()) != null) {
        builder.append(line);
      }
    }
    return builder.toString();
  }
}
