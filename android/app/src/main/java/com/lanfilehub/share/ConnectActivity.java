package com.lanfilehub.share;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.DownloadManager;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
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
 * <p>2026-10-03 第二轮（用户批准的交接单，构建号 5）：
 *   ① 「检查更新」按钮 —— 在 App 里就能查有没有新版并交给系统下载（{@link AppUpdate}）；
 *      ⛔ 最后一跳一定是系统安装器的「安装」，静默安装做不到，界面上也这么说；
 *   ② **地址丢失自愈** —— 地址簿被系统清掉/卸载重装后，打开 App 自动找回上次那台电脑并明确告诉用户。
 *
 * <p>不引 AndroidX：只用系统自带的 View/Activity，装到老手机上也不挑。
 */
public class ConnectActivity extends Activity {

  private static final long REFRESH_INTERVAL_MS = 10_000L;

  private final Handler handler = new Handler(Looper.getMainLooper());
  private final ExecutorService probeExecutor = Executors.newSingleThreadExecutor();
  /** 扫网段单独一个线程池：它要跑几秒，不能把上面的探活队列堵住 */
  private final ExecutorService discoverExecutor = Executors.newSingleThreadExecutor();
  /** 「检查更新」也单独一条：探活/扫网段在跑时也不能让它排队（一次网络请求，几秒） */
  private final ExecutorService updateExecutor = Executors.newSingleThreadExecutor();

  private ServerAdapter adapter;
  private ListView listView;
  private TextView emptyView;
  private TextView networkView;
  private TextView searchView;
  private Button updateButton;
  private boolean probing;
  private boolean discovering;
  /** 同一网络里只自动扫一次（用户手动点「找电脑」不受限制） */
  private String autoScannedFor;
  /** 这次自动扫描是为了**找回丢失的地址**（地址簿空 / 全都不在线）—— 找到了要跟用户说一声 */
  private boolean recovering;
  /** 正在检查更新（防止连点） */
  private boolean checkingUpdate;
  /** 检查到的新版（点按钮时直接下它，不用再查一次） */
  private AppUpdate.Release pendingUpdate;
  /** 本机已装的构建号（底部版本行与"检查更新"用的是同一个数） */
  private int installedBuild;
  /** 已经检查过一次更新（按钮文案据此变化） */
  private boolean updateChecked;
  /** 上次检查更新问的是哪台电脑（换了电脑就作废，见 onResume） */
  private String updateCheckedFor = "";

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
    updateButton = findViewById(R.id.check_update);
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
    // 检查更新（2026-10-03 用户批准的交接单 ①）：在 App 里就能查/就更，不依赖网页
    updateButton.setOnClickListener(view -> onUpdateButton());
  }

  @Override
  protected void onResume() {
    super.onResume();
    reloadServers();
    handler.removeCallbacks(refreshTask);
    handler.post(refreshTask);
    updateNetworkLine();
    showVersion();
    // 换了一台电脑（换了网络就会换）→ 上次查到的"有新版"不作数了，按钮退回「检查更新」
    ServerStore.Server preferred = ServerStore.preferred(this);
    String preferredKey = preferred == null ? "" : preferred.key();
    if (!preferredKey.equals(updateCheckedFor)) {
      pendingUpdate = null;
      updateChecked = false;
      updateCheckedFor = "";
    }
    applyUpdateButtonLabel();
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
    updateExecutor.shutdownNow();
  }

  /** 顶部那行"当前网络"：换网络（WiFi ↔ 热点 ↔ 流量）后回到这个页面就会更新 */
  private void updateNetworkLine() {
    NetworkState.Kind kind = NetworkState.detect(this);
    networkView.setText(getString(R.string.connect_network_label, networkLabel(kind)));
    networkView.setTextColor(NetworkState.canReachComputer(kind) ? 0xFF1F9D55 : 0xFFB45309);
  }

  /**
   * 底部那行"版本 + 构建号"（2026-10-03 用户要求：覆盖安装时能一眼分辨新旧）。
   *
   * <p>构建号由 `scripts\build-android.ps1` 每次出包自增，网页「我的」里的下载卡片显示同一个号，
   * 所以"手机上装的是不是最新"两处一对就知道。
   */
  private void showVersion() {
    TextView versionView = findViewById(R.id.app_version);
    try {
      android.content.pm.PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
      long code = android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P
          ? info.getLongVersionCode()
          : info.versionCode;
      installedBuild = (int) code;
      versionView.setText(getString(R.string.connect_version_line, info.versionName, code));
    } catch (android.content.pm.PackageManager.NameNotFoundException missing) {
      versionView.setText("");
    }
  }

  // ---- 检查更新（2026-10-03 用户批准的交接单 ①）--------------------------------

  /**
   * 「检查更新」按钮：没查过就查一次，查到新版就**下它**。
   *
   * <p>为什么两段式：用户点一次是"问有没有新版"，有新版时按钮变成「更新到构建号 N」，
   * 再点才是下载 —— 不能让"随便点一下"就开始下载安装包。
   */
  private void onUpdateButton() {
    if (checkingUpdate) {
      return;
    }
    if (pendingUpdate != null) {
      startUpdateDownload(pendingUpdate);
      return;
    }
    startUpdateCheck();
  }

  /** 问一次电脑上有没有新版（后台线程 + 主线程回报）；没有可用地址时提示先「找电脑」 */
  private void startUpdateCheck() {
    final ServerStore.Server server = ServerStore.preferred(this);
    if (server == null) {
      toast(R.string.connect_update_no_server);
      return;
    }
    checkingUpdate = true;
    updateButton.setEnabled(false);
    updateButton.setText(R.string.connect_update_checking);
    final String askedKey = server.key();
    final int current = installedBuild > 0 ? installedBuild : (int) currentVersionCode();
    updateExecutor.execute(
        () -> {
          final AppUpdate.CheckResult result = AppUpdate.check(server, current);
          handler.post(
              () -> {
                checkingUpdate = false;
                updateButton.setEnabled(true);
                updateChecked = true;
                updateCheckedFor = askedKey;
                pendingUpdate = result.updateAvailable ? result.release : null;
                applyUpdateButtonLabel();
                Toast.makeText(this, result.message, Toast.LENGTH_LONG).show();
              });
        });
  }

  /** 按钮文案：没查过 = 「检查更新」；有新版 = 「更新到构建号 N」；已最新 = 灰着写明已是最新 */
  private void applyUpdateButtonLabel() {
    if (pendingUpdate != null) {
      updateButton.setEnabled(true);
      updateButton.setText(getString(R.string.connect_update_to, pendingUpdate.versionCode));
      return;
    }
    updateButton.setText(
        updateChecked
            ? getString(R.string.connect_update_latest, installedBuild)
            : getString(R.string.connect_update_check));
    updateButton.setEnabled(!updateChecked);
  }

  /**
   * 下新版安装包：交给系统下载管理器（和网页里点「更新」是同一条路）。
   *
   * <p>⛔ <b>不会静默安装</b>：侧载应用没有 {@code INSTALL_PACKAGES}，装到最后一步必须由用户在
   * 系统安装器上点「安装」。所以提示里就把这句话说清楚，别让用户以为点完就自动装好了。
   */
  private void startUpdateDownload(AppUpdate.Release release) {
    final ServerStore.Server server = ServerStore.preferred(this);
    if (server == null) {
      toast(R.string.connect_update_no_server);
      return;
    }
    String url = release.downloadUrl(server.baseUrl());
    String fileName = DownloadNaming.sanitize(release.fileName);
    if (fileName == null || !fileName.toLowerCase(java.util.Locale.US).endsWith(".apk")) {
      fileName = "lan-file-hub.apk";
    }
    try {
      DownloadManager.Request request = new DownloadManager.Request(Uri.parse(url));
      request.setMimeType("application/vnd.android.package-archive");
      request.setTitle(fileName);
      request.setDescription(getString(R.string.connect_update_downloading, release.versionCode));
      request.setNotificationVisibility(
          DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
      // 覆盖同一个文件名，避免"下载(1).apk"越堆越多
      request.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, fileName);
      DownloadManager manager = (DownloadManager) getSystemService(DOWNLOAD_SERVICE);
      if (manager == null) {
        toast(R.string.connect_update_download_failed);
        return;
      }
      manager.enqueue(request);
      Toast.makeText(this, getString(R.string.connect_update_downloading, release.versionCode),
          Toast.LENGTH_LONG).show();
      Toast.makeText(this, R.string.connect_update_last_step, Toast.LENGTH_LONG).show();
    } catch (Exception error) {
      toast(R.string.connect_update_download_failed);
    }
  }

  /** 只在读取构建号失败时兜底用（正常情况下 {@link #installedBuild} 已经有值） */
  private long currentVersionCode() {
    try {
      android.content.pm.PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
      if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
        return info.getLongVersionCode();
      }
      return info.versionCode;
    } catch (Exception error) {
      return 0;
    }
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
      // （2026-10-03 交接单 ②：这正是"卸载重装 / 被系统清数据后地址丢失"的场景，找到要跟用户说一声）
      startDiscovery(false, true);
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
            startDiscovery(false, true);
          }
        });
  }

  /**
   * 扫当前网段找电脑。
   *
   * @param manual 用户自己点的「找电脑」：即使这个网络已经自动扫过也要再扫一次
   */
  private void startDiscovery(boolean manual) {
    startDiscovery(manual, false);
  }

  /**
   * 扫当前网段找电脑。
   *
   * @param manual             用户自己点的「找电脑」：即使这个网络已经自动扫过也要再扫一次
   * @param healLostAddresses  这次扫描是**为了找回丢失的地址**（地址簿空 / 已知地址全离线）：
   *                           找到了要明确告诉用户"已自动找回上次那台电脑"（2026-10-03 交接单 ②）
   */
  private void startDiscovery(boolean manual, boolean healLostAddresses) {
    if (discovering) {
      return;
    }
    NetworkState.Kind kind = NetworkState.detect(this);
    // ⛔ 判据是"有没有局域网地址"，不是"当前活动网络是不是 WiFi"：
    //    手机自己开热点时活动网络是移动数据，只看它就会误判成"流量连不上"而拒绝扫描
    //    （2026-10-03 用户实测："找电脑"点了没反应 → 就是这个原因）。
    final String ownIp = NetUtil.lanIp();
    if (ownIp == null || ownIp.isEmpty() || !NetworkState.canReachComputer(kind)) {
      // 真的没有局域网地址（纯流量）：扫也没用，直接说清楚，别让用户白等
      searchView.setText(R.string.connect_search_no_lan);
      return;
    }
    // 扫的端口：便携包默认端口 + 历史里用过的端口（用户以前连过别的端口时也要能找到）
    final int[] ports = ServerDiscovery.portsToScan(ServerStore.defaultPort(this));
    final String scanKey = ownIp + ":" + ports[0] + (ports.length > 1 ? ("," + ports[1]) : "");
    if (!manual && scanKey.equals(autoScannedFor)) {
      return;  // 这个网络这次已经扫过了
    }
    autoScannedFor = scanKey;
    discovering = true;
    // 这次自动扫描要不要"找回"：地址簿本来就是空的，或者全都不在线（用户手动点的不算 —— 他知道自己在干嘛）
    recovering = healLostAddresses && !manual;
    searchView.setText(getString(R.string.connect_searching, 0, 0));

    discoverExecutor.execute(
        () -> {
          final int prefixLength = ServerDiscovery.prefixLengthOf(ownIp);
          List<ServerDiscovery.Found> found =
              ServerDiscovery.scan(
                  ownIp,
                  prefixLength,
                  ports,
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
          // 找回来了没有（这次就是为了找回地址才扫的，见 startDiscovery 的 healLostAddresses 参数）
          final boolean healed = recovering && !found.isEmpty();
          final String scanRange = ownIp.substring(0, ownIp.lastIndexOf('.') + 1) + "1~254";
          final String portsText = ports.length > 1 ? (ports[0] + "、" + ports[1]) : String.valueOf(ports[0]);
          handler.post(
              () -> {
                discovering = false;
                recovering = false;
                if (changed) {
                  reloadServers();
                }
                if (healed) {
                  // 2026-10-03 交接单 ②：地址簿被系统清掉/卸载重装后，用户最需要知道的是
                  // "上次那台电脑自己回来了"，而不是一串扫描数字（AGENTS §14.1）
                  searchView.setText(
                      getString(R.string.connect_healed, found.get(0).host, String.valueOf(found.get(0).port))
                          + "\n"
                          + getString(R.string.connect_found, found.size()));
                } else if (!found.isEmpty()) {
                  searchView.setText(getString(R.string.connect_found, found.size()));
                } else {
                  // 把"扫了什么"也说清楚：用户能据此判断是不是网络问题（AGENTS §14.1）
                  // 2026-10-03 用户实测补丁：校园网是 /16 大网段，手机只扫自己那一段 ——
                  // 这种情况**不是**网络禁止互访，必须让用户去「添加地址」，不能冤枉网络。
                  final boolean wide = NetUtil.isWideSubnet(NetUtil.prefixLength());
                  searchView.setText(
                      (kind == NetworkState.Kind.HOTSPOT
                              ? getString(R.string.connect_not_found_hotspot)
                              : wide
                                  ? getString(R.string.connect_not_found_wide_subnet)
                                  : getString(R.string.connect_not_found_wifi))
                          + "\n"
                          + getString(R.string.connect_search_scope, ownIp, scanRange, portsText));
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
