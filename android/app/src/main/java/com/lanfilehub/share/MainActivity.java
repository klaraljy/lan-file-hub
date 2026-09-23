package com.lanfilehub.share;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.google.zxing.BarcodeFormat;
import com.google.zxing.WriterException;
import com.google.zxing.common.BitMatrix;
import com.google.zxing.qrcode.QRCodeWriter;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;

/**
 * 「局域网快传」主界面（面向不懂技术的人：三步走，按钮大、话少）。
 *
 * <p>1) 选择要分享的文件 → 2) 开始分享 → 3) 让对方连同一个 WiFi/热点并扫码。
 *
 * <p>刻意不引入 AndroidX：本页只用系统自带的 View/Activity，依赖少、构建快、装到老手机上也不挑。
 */
public class MainActivity extends Activity {

  private static final int REQUEST_PICK_FILES = 1001;
  private static final int REQUEST_NOTIFICATION = 1002;

  private TextView statusView;
  private TextView urlView;
  private TextView hintView;
  private ImageView qrView;
  private LinearLayout fileListView;
  private Button toggleButton;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    statusView = findViewById(R.id.status_text);
    urlView = findViewById(R.id.url_text);
    hintView = findViewById(R.id.hint_text);
    qrView = findViewById(R.id.qr_image);
    fileListView = findViewById(R.id.file_list);
    toggleButton = findViewById(R.id.toggle_service);

    findViewById(R.id.add_files).setOnClickListener(view -> pickFiles());
    toggleButton.setOnClickListener(view -> toggleSharing());
    findViewById(R.id.clear_files).setOnClickListener(view -> confirmClearFiles());

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
        && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
        != PackageManager.PERMISSION_GRANTED) {
      requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS},
          REQUEST_NOTIFICATION);
    }
  }

  @Override
  protected void onResume() {
    super.onResume();
    refreshUi();
  }

  // --------------------------------------------------------------- 选择文件

  private void pickFiles() {
    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
    intent.addCategory(Intent.CATEGORY_OPENABLE);
    intent.setType("*/*");
    intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
    startActivityForResult(intent, REQUEST_PICK_FILES);
  }

  @Override
  protected void onActivityResult(int requestCode, int resultCode, Intent data) {
    super.onActivityResult(requestCode, resultCode, data);
    if (requestCode != REQUEST_PICK_FILES || resultCode != RESULT_OK || data == null) {
      return;
    }
    if (data.getClipData() != null) {
      int count = data.getClipData().getItemCount();
      for (int index = 0; index < count; index++) {
        copyIntoShareDir(data.getClipData().getItemAt(index).getUri());
      }
    } else if (data.getData() != null) {
      copyIntoShareDir(data.getData());
    }
  }

  /** 把选中的文件复制进分享目录（1 GB 以内直接复制，避免 SAF 树授权的复杂度） */
  private void copyIntoShareDir(Uri uri) {
    Toast.makeText(this, "正在复制…", Toast.LENGTH_SHORT).show();
    new Thread(() -> {
      String name = displayName(uri);
      File target = new File(ShareController.shareDir(this), name);
      String failure = null;
      try (InputStream input = getContentResolver().openInputStream(uri);
          OutputStream output = new FileOutputStream(target)) {
        if (input == null) {
          failure = "读不到这个文件";
        } else {
          byte[] buffer = new byte[64 * 1024];
          int read;
          while ((read = input.read(buffer)) > 0) {
            output.write(buffer, 0, read);
          }
        }
      } catch (IOException e) {
        failure = e.getMessage();
      }
      final String message = failure == null ? "已加入：" + name : "复制失败：" + failure;
      runOnUiThread(() -> {
        Toast.makeText(this, message, Toast.LENGTH_SHORT).show();
        refreshUi();
      });
    }, "copy-file").start();
  }

  private String displayName(Uri uri) {
    String name = null;
    try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
      if (cursor != null && cursor.moveToFirst()) {
        int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
        if (index >= 0) {
          name = cursor.getString(index);
        }
      }
    } catch (Exception ignored) {
      // 拿不到就用路径最后一段兜底
    }
    if (name == null || name.trim().isEmpty()) {
      String path = uri.getLastPathSegment();
      name = path == null ? "file" : path.replace('/', '_');
    }
    // 去掉路径分隔符，保证只能落到分享目录里
    return name.replace('/', '_').replace('\\', '_');
  }

  // --------------------------------------------------------------- 启停分享

  private void toggleSharing() {
    if (ShareController.isRunning()) {
      ShareController.stop();
      stopService(new Intent(this, ShareService.class));
      Toast.makeText(this, "已停止分享", Toast.LENGTH_SHORT).show();
      refreshUi();
      return;
    }
    if (NetUtil.lanIp() == null) {
      new AlertDialog.Builder(this)
          .setTitle("还没有网络")
          .setMessage("请先连接 WiFi，或者打开手机热点，然后再点「开始分享」。")
          .setPositiveButton("知道了", null)
          .show();
      return;
    }
    try {
      ShareController.start(this);
      startForegroundService(new Intent(this, ShareService.class));
      Toast.makeText(this, "已开始分享，让对方扫码", Toast.LENGTH_SHORT).show();
    } catch (IOException e) {
      new AlertDialog.Builder(this)
          .setTitle("启动失败")
          .setMessage("端口被占用或存储不可用：" + e.getMessage())
          .setPositiveButton("知道了", null)
          .show();
    }
    refreshUi();
  }

  private void confirmClearFiles() {
    new AlertDialog.Builder(this)
        .setTitle("清空已分享的文件？")
        .setMessage("会把「局域网快传」目录里的文件都删掉（原文件在别处不受影响）。")
        .setNegativeButton("取消", null)
        .setPositiveButton("清空", (dialog, which) -> {
          File[] files = ShareController.shareDir(this).listFiles();
          if (files != null) {
            for (File file : files) {
              //noinspection ResultOfMethodCallIgnored
              file.delete();
            }
          }
          refreshUi();
        })
        .show();
  }

  // ------------------------------------------------------------------ 界面

  private void refreshUi() {
    boolean running = ShareController.isRunning();
    String url = ShareController.shareUrl();

    toggleButton.setText(running ? "停止分享" : "开始分享");
    statusView.setText(running ? "正在分享（保持这个页面或后台都行）" : "还没有开始分享");
    if (running && url != null) {
      urlView.setText(url);
      urlView.setVisibility(TextView.VISIBLE);
      qrView.setImageBitmap(createQrBitmap(url));
      qrView.setVisibility(ImageView.VISIBLE);
      String ip = NetUtil.lanIp();
      hintView.setText(NetUtil.looksLikeHotspot(ip)
          ? "让对方连上你的手机热点，然后用相机或微信扫上面的二维码。"
          : "让对方连同一个 WiFi，然后用相机或微信扫上面的二维码。");
    } else {
      urlView.setVisibility(TextView.GONE);
      qrView.setVisibility(ImageView.GONE);
      hintView.setText("点「开始分享」后，这里会出现地址和二维码。");
    }

    fileListView.removeAllViews();
    List<File> files = ShareController.current() == null
        ? java.util.Collections.emptyList()
        : ShareController.current().listFiles();
    if (files.isEmpty()) {
      TextView empty = new TextView(this);
      empty.setText("还没有文件。点上面的「选择要分享的文件」加进来。");
      empty.setTextSize(14f);
      fileListView.addView(empty);
    } else {
      for (File file : files) {
        TextView row = new TextView(this);
        row.setText("• " + file.getName() + "（" + ShareHttpServer.formatSize(file.length()) + "）");
        row.setTextSize(15f);
        row.setPadding(0, 10, 0, 10);
        fileListView.addView(row, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
      }
    }
  }

  private Bitmap createQrBitmap(String content) {
    try {
      BitMatrix matrix = new QRCodeWriter().encode(content, BarcodeFormat.QR_CODE, 512, 512);
      int[] pixels = new int[matrix.getWidth() * matrix.getHeight()];
      for (int y = 0; y < matrix.getHeight(); y++) {
        for (int x = 0; x < matrix.getWidth(); x++) {
          pixels[y * matrix.getWidth() + x] = matrix.get(x, y) ? 0xFF000000 : 0xFFFFFFFF;
        }
      }
      return Bitmap.createBitmap(pixels, matrix.getWidth(), matrix.getHeight(),
          Bitmap.Config.ARGB_8888);
    } catch (WriterException e) {
      return null;
    }
  }
}
