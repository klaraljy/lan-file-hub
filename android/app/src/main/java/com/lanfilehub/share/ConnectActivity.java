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
 * <p>不引 AndroidX：只用系统自带的 View/Activity，装到老手机上也不挑。
 */
public class ConnectActivity extends Activity {

  private static final long REFRESH_INTERVAL_MS = 10_000L;

  private final Handler handler = new Handler(Looper.getMainLooper());
  private final ExecutorService probeExecutor = Executors.newSingleThreadExecutor();

  private ServerAdapter adapter;
  private ListView listView;
  private TextView emptyView;
  private boolean probing;

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
    listView.setAdapter(adapter);

    listView.setOnItemClickListener(
        (parent, view, position, id) -> openServer((ServerStore.Server) adapter.getItem(position)));

    listView.setOnItemLongClickListener(
        (parent, view, position, id) -> {
          showActions((ServerStore.Server) adapter.getItem(position));
          return true;
        });

    findViewById(R.id.add_server).setOnClickListener(view -> showAddDialog());
    findViewById(R.id.open_share).setOnClickListener(
        view -> startActivity(new Intent(this, MainActivity.class)));
  }

  @Override
  protected void onResume() {
    super.onResume();
    reloadServers();
    handler.removeCallbacks(refreshTask);
    handler.post(refreshTask);
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
      return;
    }
    probeExecutor.execute(
        () -> {
          for (ServerStore.Server server : servers) {
            final boolean online = ServerProbe.isOnline(server);
            handler.post(
                () ->
                    adapter.setStatus(
                        server,
                        online ? ServerAdapter.Status.ONLINE : ServerAdapter.Status.OFFLINE));
          }
          probing = false;
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
