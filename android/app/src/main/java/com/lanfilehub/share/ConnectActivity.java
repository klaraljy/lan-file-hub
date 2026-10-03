package com.lanfilehub.share;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 「连接电脑」页 —— App 的首页（用户 2026-10-02：手机上网页和 App 都要有；
 * App 里要有历史 IP、能选一个连、并显示那个 IP 在不在线）。
 *
 * <p>刻意走"App 内打开电脑网页"这条简单路线：文件列表、上传下载、分享全部复用电脑端网页，
 * App 只负责**记住地址 + 探活 + 用内置浏览器打开**。这样网页改一次，App 不用跟着改。
 *
 * <p>2026-10-03 用户要求"点开 App 就应该自动检测连上没有"，所以这里多了两件事：
 *   ① 顶部显示**当前网络**（WiFi / 手机热点 / 流量 —— 不读 WiFi 名字，避免要定位权限）；
 *   ② 已知地址都不在线时**自动扫一遍当前网段**找电脑（{@link ServerDiscovery}），
 *      扫不到就按网络类型给出人话解释（流量连不上 / 可能是客户端隔离，请改用手机热点）。
 *
 * <p>不引 AndroidX：只用系统自带的 View/Activity，装到老手机上也不挑。
 */
public class ConnectActivity extends Activity {

  private static final long REFRESH_INTERVAL_MS = 10_000L;

  private final Handler handler = new Handler(Looper.getMainLooper());
  private final ExecutorService probeExecutor = Executors.newSingleThreadExecutor();
  /** 扫网段单独一个线程池：它要跑几秒，不能把上面的探活队列堵住 */
  private final ExecutorService discoverExecutor = Executors.newSingleThreadExecutor();

  private ServerAdapter adapter;
  private ListView listView;
  private TextView emptyView;
  private TextView networkView;
  private TextView searchView;
  private boolean probing;
  private boolean discovering;
  /** 同一网络里只自动扫一次（用户手动点「找电脑」不受限制） */
  private String autoScannedFor;

  private final Runnable refreshTask =
      new Runnable() {
        @Override
        public void run() {
          probeAll();
          handler.postDelayed(this, REFRESH_INTERVAL_MS);
        }
      };

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_connect);

    adapter = new ServerAdapter(this);
    listView = findViewById(R.id.server_list);
    emptyView = findViewById(R.id.empty_hint);
    networkView = findViewById(R.id.network_state);
    searchView = findViewById(R.id.search_state);
    listView.setAdapter(adapter);

    listView.setOnItemClickListener(
        (parent, view, position, id) -> openServer((ServerStore.Server) adapter.getItem(position)));

    listView.setOnItemLongClickListener(
        (parent, view, position, id) -> {
          showActions((ServerStore.Server) adapter.getItem(position));
          return true;
        });

    findViewById(R.id.add_server).setOnClickListener(view -> showAddDialog());
    findViewById(R.id.find_server).setOnClickListener(view -> startDiscovery(true));
    findViewById(R.id.open_share).setOnClickListener(
        view -> startActivity(new Intent(this, MainActivity.class)));
    // 相册备份（2026-10-03）：把手机照片存到电脑上
    findViewById(R.id.open_backup).setOnClickListener(
        view -> startActivity(new Intent(this, BackupActivity.class)));
  }

  @Override
  protected void onResume() {
    super.onResume();
    reloadServers();
    handler.removeCallbacks(refreshTask);
    handler.post(refreshTask);
    updateNetworkLine();
  }

  @Override
  protected void onPause() {
    super.onPause();
    handler.removeCallbacks(refreshTask);
  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
    probeExecutor.shutdownNow();
    discoverExecutor.shutdownNow();
  }

  /** 顶部那行"当前网络"：换网络（WiFi ↔ 热点 ↔ 流量）后回到这个页面就会更新 */
  private void updateNetworkLine() {
    NetworkState.Kind kind = NetworkState.detect(this);
    networkView.setText(getString(R.string.connect_network_label, networkLabel(kind)));
    networkView.setTextColor(NetworkState.canReachComputer(kind) ? 0xFF1F9D55 : 0xFFB45309);
  }

  private String networkLabel(NetworkState.Kind kind) {
    switch (kind) {
      case WIFI:
        return getString(R.string.network_wifi);
      case HOTSPOT:
        return getString(R.string.network_hotspot);
      case ETHERNET:
        return getString(R.string.network_ethernet);
      case CELLULAR:
        return getString(R.string.network_cellular);
      case OTHER:
        return getString(R.string.network_other);
      default:
        return getString(R.string.network_none);
    }
  }

  private void reloadServers() {
    List<ServerStore.Server> servers = ServerStore.load(this);
    adapter.setServers(servers);
    adapter.clearStatuses();
    emptyView.setVisibility(servers.isEmpty() ? View.VISIBLE : View.GONE);
    listView.setVisibility(servers.isEmpty() ? View.GONE : View.VISIBLE);
  }

  /** 逐条探活：在后台线程做，避免卡住界面；结果回到主线程刷新。 */
  private void probeAll() {
    if (probing) {
      return;
    }
    probing = true;
    final List<ServerStore.Server> servers = adapter.getServers();
    if (servers.isEmpty()) {
      probing = false;
      // 一条记录都没有：直接开始自动查找，用户不用先"添加电脑"
      startDiscovery(false);
      return;
    }
    probeExecutor.execute(
        () -> {
          boolean anyOnline = false;
          for (ServerStore.Server server : servers) {
            final boolean online = ServerProbe.isOnline(server);
            anyOnline = anyOnline || online;
            handler.post(
                () ->
                    adapter.setStatus(
                        server,
                        online ? ServerAdapter.Status.ONLINE : ServerAdapter.Status.OFFLINE));
          }
          probing = false;
          // 已知地址全都不在线（换网络后旧地址就死了）→ 自动扫一遍当前网段
          if (!anyOnline) {
            startDiscovery(false);
          }
        });
  }

  /**
   * 扫当前网段找电脑。
   *
   * @param manual 用户自己点的「找电脑」：即使这个网络已经自动扫过也要再扫一次
   */
  private void startDiscovery(boolean manual) {
    if (discovering) {
      return;
    }
    NetworkState.Kind kind = NetworkState.detect(this);
    if (!NetworkState.canReachComputer(kind)) {
      // 流量下扫也没用（电脑不在移动网络里），直接说清楚，别让用户白等
      searchView.setText(R.string.connect_search_no_lan);
      return;
    }
    final String ownIp = NetUtil.lanIp();
    if (ownIp == null) {
      searchView.setText(R.string.connect_search_no_ip);
      return;
    }
    final int port = ServerStore.defaultPort(this);
    final String scanKey = ownIp + ":" + port;
    if (!manual && scanKey.equals(autoScannedFor)) {
      return;  // 这个网络这次已经扫过了
    }
    autoScannedFor = scanKey;
    discovering = true;
    searchView.setText(getString(R.string.connect_searching, 0, 0));

    discoverExecutor.execute(
        () -> {
          List<ServerDiscovery.Found> found =
              ServerDiscovery.scan(
                  ownIp,
                  port,
                  (done, total) ->
                      handler.post(
                          () -> searchView.setText(getString(R.string.connect_searching, done, total))));

          boolean added = false;
          for (ServerDiscovery.Found server : found) {
            ServerStore.Server known = ServerStore.find(this, server.host, server.port);
            if (known == null) {
              ServerStore.addOrUpdate(
                  this,
                  new ServerStore.Server(
                      server.host,
                      server.port,
                      getString(R.string.connect_discovered_label),
                      System.currentTimeMillis()));
              added = true;
            }
          }
          final boolean changed = added;
          handler.post(
              () -> {
                discovering = false;
                if (changed) {
                  reloadServers();
                }
                if (!found.isEmpty()) {
                  searchView.setText(getString(R.string.connect_found, found.size()));
                } else {
                  searchView.setText(
                      kind == NetworkState.Kind.HOTSPOT
                          ? getString(R.string.connect_not_found_hotspot)
                          : getString(R.string.connect_not_found_wifi));
                }
                probeAll();
              });
        });
  }

  private void openServer(ServerStore.Server server) {
    ServerStore.touch(this, server);
    Intent intent = new Intent(this, WebViewActivity.class);
    intent.putExtra(WebViewActivity.EXTRA_URL, server.baseUrl() + "/");
    intent.putExtra(WebViewActivity.EXTRA_TITLE, server.displayName());
    startActivity(intent);
  }

  /**
   * 长按一条：编辑地址 / 删除。
   *
   * <p>为什么必须有"编辑地址"（用户 2026-10-02 实测连不上）：电脑换网络（校园网 ↔ 手机热点）
   * 之后局域网地址会变，存的旧地址就永远连不上 —— 用户得能改，而不是只能删了重加。
   */
  private void showActions(ServerStore.Server server) {
    new AlertDialog.Builder(this)
        .setTitle(server.displayName() + "（" + server.baseUrl() + "）")
        .setItems(
            new CharSequence[] {getString(R.string.connect_action_edit), getString(R.string.connect_action_remove)},
            (dialog, which) -> {
              if (which == 0) {
                showEditDialog(server);
              } else {
                confirmRemove(server);
              }
            })
        .show();
  }

  /** 编辑地址：输入框里预填当前地址，确认后按新地址重写这条记录 */
  private void showEditDialog(ServerStore.Server server) {
    final EditText input = new EditText(this);
    input.setInputType(InputType.TYPE_TEXT_VARIATION_URI);
    input.setText(server.host + ":" + server.port);
    LinearLayout wrapper = new LinearLayout(this);
    wrapper.setPadding(48, 16, 48, 0);
    wrapper.addView(input);

    new AlertDialog.Builder(this)
        .setTitle(R.string.connect_edit_title)
        .setMessage(R.string.connect_edit_message)
        .setView(wrapper)
        .setNegativeButton(R.string.connect_cancel, null)
        .setPositiveButton(
            R.string.connect_edit_confirm,
            (dialog, which) -> {
              ServerStore.Server parsed = ServerStore.parse(input.getText().toString());
              if (parsed == null) {
                toast(R.string.connect_add_invalid);
                return;
              }
              ServerStore.remove(this, server);
              ServerStore.addOrUpdate(this, parsed);
              reloadServers();
              probeAll();
              toast(R.string.connect_edit_done);
            })
        .show();
  }

  private void showAddDialog() {
    final EditText input = new EditText(this);
    input.setInputType(InputType.TYPE_TEXT_VARIATION_URI);
    input.setHint(R.string.connect_add_hint);
    LinearLayout wrapper = new LinearLayout(this);
    wrapper.setPadding(48, 16, 48, 0);
    wrapper.addView(input);

    new AlertDialog.Builder(this)
        .setTitle(R.string.connect_add_title)
        .setMessage(R.string.connect_add_message)
        .setView(wrapper)
        .setNegativeButton(R.string.connect_cancel, null)
        .setPositiveButton(
            R.string.connect_add_confirm,
            (dialog, which) -> {
              ServerStore.Server parsed = ServerStore.parse(input.getText().toString());
              if (parsed == null) {
                toast(R.string.connect_add_invalid);
                return;
              }
              ServerStore.addOrUpdate(this, parsed);
              reloadServers();
              probeAll();
            })
        .show();
  }

  private void confirmRemove(ServerStore.Server server) {
    new AlertDialog.Builder(this)
        .setTitle(R.string.connect_remove_title)
        .setMessage(getString(R.string.connect_remove_message, server.displayName()))
        .setNegativeButton(R.string.connect_cancel, null)
        .setPositiveButton(
            R.string.connect_remove_confirm,
            (dialog, which) -> {
              ServerStore.remove(this, server);
              reloadServers();
            })
        .show();
  }

  private void toast(int stringRes) {
    Toast.makeText(this, stringRes, Toast.LENGTH_SHORT).show();
  }
}
