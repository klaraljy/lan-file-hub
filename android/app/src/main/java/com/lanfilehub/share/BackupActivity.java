package com.lanfilehub.share;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 「相册备份」页（2026-10-03，用户批准的功能之一）。
 *
 * <p>用户口径：手机相册里的照片，在 WiFi 下自动/手动备份到电脑上的文件柜里。
 * 这一页要自己说清四件事（AGENTS §14.1：不会用 = 缺陷）：
 *   ① 现在连的是哪台电脑、用哪个账号（没配就引导去「连接电脑」并在这里登录）；
 *   ② 要备份哪些相册（勾选）；
 *   ③ 传过的不会重复传（本地清单）；
 *   ④ 现在能不能备份（只在 WiFi/热点下，流量下会停下并说明原因）。
 *
 * <p>权限：读相册需要 READ_MEDIA_IMAGES（Android 13+）或 READ_EXTERNAL_STORAGE（≤12）。
 * 这是功能必需的权限，页面上会说明用途；⛔ 不申请定位权限（用户 2026-10-03 明确选择）。
 */
public class BackupActivity extends Activity {

  private static final int REQUEST_MEDIA = 4101;

  private final Handler handler = new Handler(Looper.getMainLooper());
  private final ExecutorService executor = Executors.newSingleThreadExecutor();

  private TextView serverView;
  private TextView statusView;
  private TextView bucketSummaryView;
  private LinearLayout bucketList;
  private CheckBox wifiOnly;
  private Button startButton;
  private Button stopButton;

  private final BackupController.Listener listener =
      state ->
          handler.post(
              () -> {
                statusView.setText(
                    state.message.isEmpty() ? "准备就绪" : state.message + (state.lastError.isEmpty() ? "" : "\n最近一次失败：" + state.lastError));
                startButton.setEnabled(!state.running);
                stopButton.setEnabled(state.running);
              });

  private final Runnable refreshTask =
      new Runnable() {
        @Override
        public void run() {
          updateServerLine();
          handler.postDelayed(this, 3000);
        }
      };

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_backup);

    serverView = findViewById(R.id.backup_server);
    statusView = findViewById(R.id.backup_status);
    bucketSummaryView = findViewById(R.id.backup_bucket_summary);
    bucketList = findViewById(R.id.backup_bucket_list);
    wifiOnly = findViewById(R.id.backup_wifi_only);
    startButton = findViewById(R.id.backup_start);
    stopButton = findViewById(R.id.backup_stop);

    wifiOnly.setChecked(BackupStore.wifiOnly(this));
    wifiOnly.setOnCheckedChangeListener(
        (button, checked) -> BackupStore.setWifiOnly(this, checked));

    findViewById(R.id.backup_login).setOnClickListener(view -> showLoginDialog());
    findViewById(R.id.backup_choose_buckets).setOnClickListener(view -> showBucketDialog());
    startButton.setOnClickListener(view -> requestPermissionThenStart());
    stopButton.setOnClickListener(view -> BackupController.get(this).stop());
  }

  @Override
  protected void onResume() {
    super.onResume();
    BackupController.get(this).addListener(listener);
    handler.removeCallbacks(refreshTask);
    handler.post(refreshTask);
    updateServerLine();
    loadBuckets();
  }

  @Override
  protected void onPause() {
    super.onPause();
    BackupController.get(this).removeListener(listener);
    handler.removeCallbacks(refreshTask);
  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
    executor.shutdownNow();
  }

  /** 顶部那行：现在备份到哪台电脑、用哪个账号 */
  private void updateServerLine() {
    if (!BackupStore.isConfigured(this)) {
      serverView.setText(R.string.backup_not_connected);
      return;
    }
    String account = BackupStore.username(this);
    serverView.setText(getString(R.string.backup_target, BackupStore.host(this),
        String.valueOf(BackupStore.port(this)), account.isEmpty() ? "未登录" : account));
  }

  /** 读相册列表（后台线程，避免大相册卡界面） */
  private void loadBuckets() {
    executor.execute(
        () -> {
          List<MediaScanner.Bucket> buckets = MediaScanner.buckets(this);
          Set<String> selected = BackupStore.buckets(this);
          handler.post(
              () -> {
                bucketList.removeAllViews();
                if (buckets.isEmpty()) {
                  bucketSummaryView.setText(R.string.backup_no_buckets);
                  return;
                }
                int selectedCount = 0;
                for (MediaScanner.Bucket bucket : buckets) {
                  CheckBox box = new CheckBox(this);
                  box.setText(bucket.toString());
                  box.setTextSize(15f);
                  box.setChecked(selected.contains(bucket.name));
                  if (box.isChecked()) {
                    selectedCount++;
                  }
                  box.setOnCheckedChangeListener(
                      (button, checked) -> {
                        Set<String> current = BackupStore.buckets(this);
                        if (checked) {
                          current.add(bucket.name);
                        } else {
                          current.remove(bucket.name);
                        }
                        BackupStore.setBuckets(this, current);
                        updateBucketSummary(current.size());
                      });
                  bucketList.addView(box);
                }
                updateBucketSummary(selectedCount);
              });
        });
  }

  private void updateBucketSummary(int selectedCount) {
    String last = BackupStore.lastResult(this);
    bucketSummaryView.setText(
        getString(R.string.backup_bucket_summary, selectedCount)
            + (last.isEmpty() ? "" : "\n上次结果：" + last));
  }

  /** 登录：拿到令牌才能上传（令牌只存本机，不存口令） */
  private void showLoginDialog() {
    if (ServerStore.load(this).isEmpty()) {
      new AlertDialog.Builder(this)
          .setTitle(R.string.backup_need_server_title)
          .setMessage(R.string.backup_need_server_message)
          .setPositiveButton(R.string.backup_goto_connect,
              (dialog, which) -> startActivity(new Intent(this, ConnectActivity.class)))
          .setNegativeButton(R.string.connect_cancel, null)
          .show();
      return;
    }

    final EditText user = new EditText(this);
    user.setInputType(InputType.TYPE_CLASS_TEXT);
    user.setHint(R.string.backup_login_user_hint);
    user.setText(BackupStore.username(this));
    final EditText password = new EditText(this);
    password.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
    password.setHint(R.string.backup_login_password_hint);
    LinearLayout wrapper = new LinearLayout(this);
    wrapper.setOrientation(LinearLayout.VERTICAL);
    wrapper.setPadding(48, 16, 48, 0);
    wrapper.addView(user);
    wrapper.addView(password);

    new AlertDialog.Builder(this)
        .setTitle(R.string.backup_login_title)
        .setMessage(R.string.backup_login_message)
        .setView(wrapper)
        .setNegativeButton(R.string.connect_cancel, null)
        .setPositiveButton(
            R.string.backup_login_confirm,
            (dialog, which) -> doLogin(user.getText().toString().trim(),
                password.getText().toString()))
        .show();
  }

  private void doLogin(String username, String password) {
    if (username.isEmpty() || password.isEmpty()) {
      toast(R.string.backup_login_empty);
      return;
    }
    List<ServerStore.Server> servers = ServerStore.load(this);
    if (servers.isEmpty()) {
      return;
    }
    // 用列表里"最近用的那台"（ServerStore.load 已按最近使用排序）
    final ServerStore.Server server = servers.get(0);
    statusView.setText(R.string.backup_login_working);
    executor.execute(
        () -> {
          String message;
          try {
            String token = ApiClient.login(server.host, server.port, username, password);
            BackupStore.saveLogin(this, server, username, token);
            message = getString(R.string.backup_login_ok, server.displayName());
          } catch (Exception error) {
            message = getString(R.string.backup_login_failed, String.valueOf(error.getMessage()));
          }
          final String result = message;
          handler.post(
              () -> {
                statusView.setText(result);
                updateServerLine();
                toast(result);
              });
        });
  }

  /** 选相册：用一个多选列表弹窗 */
  private void showBucketDialog() {
    executor.execute(
        () -> {
          List<MediaScanner.Bucket> buckets = MediaScanner.buckets(this);
          handler.post(
              () -> {
                if (buckets.isEmpty()) {
                  toast(R.string.backup_no_buckets_toast);
                  requestMediaPermission();
                  return;
                }
                final String[] labels = new String[buckets.size()];
                final boolean[] checked = new boolean[buckets.size()];
                Set<String> selected = BackupStore.buckets(this);
                for (int index = 0; index < buckets.size(); index++) {
                  labels[index] = buckets.get(index).toString();
                  checked[index] = selected.contains(buckets.get(index).name);
                }
                new AlertDialog.Builder(this)
                    .setTitle(R.string.backup_choose_title)
                    .setMultiChoiceItems(labels, checked,
                        (dialog, which, isChecked) -> checked[which] = isChecked)
                    .setNegativeButton(R.string.connect_cancel, null)
                    .setPositiveButton(
                        R.string.backup_choose_confirm,
                        (dialog, which) -> {
                          Set<String> chosen = new HashSet<>();
                          for (int index = 0; index < buckets.size(); index++) {
                            if (checked[index]) {
                              chosen.add(buckets.get(index).name);
                            }
                          }
                          BackupStore.setBuckets(this, chosen);
                          loadBuckets();
                        })
                    .show();
              });
        });
  }

  // ---- 权限 ---------------------------------------------------------------

  private String mediaPermission() {
    return Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
        ? Manifest.permission.READ_MEDIA_IMAGES
        : Manifest.permission.READ_EXTERNAL_STORAGE;
  }

  private void requestPermissionThenStart() {
    if (checkSelfPermission(mediaPermission()) != PackageManager.PERMISSION_GRANTED) {
      requestMediaPermission();
      return;
    }
    startBackup();
  }

  private void requestMediaPermission() {
    requestPermissions(new String[] {mediaPermission()}, REQUEST_MEDIA);
  }

  @Override
  public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                         int[] grantResults) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    if (requestCode != REQUEST_MEDIA) {
      return;
    }
    if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
      loadBuckets();
      if (BackupStore.buckets(this).isEmpty()) {
        showBucketDialog();
      }
    } else {
      toast(R.string.backup_permission_denied);
    }
  }

  private void startBackup() {
    if (BackupStore.buckets(this).isEmpty()) {
      toast(R.string.backup_pick_bucket_first);
      showBucketDialog();
      return;
    }
    // 先算清楚"要传多少张、多大"，再让用户确认（用户 2026-10-03 明确要求：
    // "这个应该是可选择性传输的吧，你不会点一下就全备份了吧"）。
    // 统计要读 MediaStore，放后台线程，别卡界面。
    statusView.setText(R.string.backup_calculating);
    executor.execute(
        () -> {
          BackupPlanner.Summary summary = BackupController.preview(this);
          boolean uploadable = BackupController.canUploadNow(this);
          handler.post(() -> confirmAndStart(summary, uploadable));
        });
  }

  /** 确认框：把"将上传多少"摆在明面上，用户按「开始备份」才真的传。 */
  private void confirmAndStart(BackupPlanner.Summary summary, boolean uploadable) {
    if (summary.isEmpty()) {
      statusView.setText(R.string.backup_nothing_new);
      toast(R.string.backup_nothing_new);
      return;
    }
    String sizeText = formatSize(summary.totalBytes);
    StringBuilder message = new StringBuilder();
    message.append(getString(R.string.backup_confirm_message, summary.count, sizeText));
    if (!uploadable) {
      message.append("\n\n").append(getString(R.string.backup_confirm_not_wifi));
    }
    statusView.setText(getString(R.string.backup_idle));
    new AlertDialog.Builder(this)
        .setTitle(R.string.backup_confirm_title)
        .setMessage(message.toString())
        .setNegativeButton(R.string.connect_cancel, null)
        .setPositiveButton(
            R.string.backup_confirm_start,
            (dialog, which) -> {
              // 前台服务：切到后台/锁屏也不至于被系统立刻回收
              startForegroundService(new Intent(this, BackupService.class));
              BackupController.get(this).start();
            })
        .show();
  }

  /** 把字节数说成人话（KB / MB / GB） */
  private static String formatSize(long bytes) {
    if (bytes < 1024) {
      return bytes + " B";
    }
    if (bytes < 1024L * 1024L) {
      return Math.round(bytes / 1024.0) + " KB";
    }
    if (bytes < 1024L * 1024L * 1024L) {
      return String.format(java.util.Locale.ROOT, "%.1f MB", bytes / 1024.0 / 1024.0);
    }
    return String.format(java.util.Locale.ROOT, "%.2f GB", bytes / 1024.0 / 1024.0 / 1024.0);
  }

  private void toast(int stringRes) {
    Toast.makeText(this, stringRes, Toast.LENGTH_LONG).show();
  }

  private void toast(String text) {
    Toast.makeText(this, text, Toast.LENGTH_LONG).show();
  }
}
