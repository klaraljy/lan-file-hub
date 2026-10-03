package com.lanfilehub.share;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

/**
 * 相册备份的前台服务（2026-10-03）：备份可能传几百张照片，切到后台时系统会把普通后台线程
 * 回收掉，所以必须挂一个前台服务 + 常驻通知（通知里显示进度，用户随时知道在做什么）。
 *
 * <p>真正的活在 {@link BackupController} 的后台线程里；这里只负责"让进程活着 + 显示进度"。
 */
public class BackupService extends Service {

  private static final String CHANNEL_ID = "lan-backup";
  private static final int NOTIFICATION_ID = 1002;

  private final Handler handler = new Handler(Looper.getMainLooper());
  private final BackupController.Listener listener = state -> handler.post(this::update);

  private final Runnable poll =
      new Runnable() {
        @Override
        public void run() {
          update();
          handler.postDelayed(this, 2000);
        }
      };

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    createChannel();
    startForegroundCompat(buildNotification("正在准备备份…"));
    BackupController.get(this).addListener(listener);
    handler.removeCallbacks(poll);
    handler.post(poll);
    return START_NOT_STICKY;
  }

  private void update() {
    BackupController.State state = BackupController.get(this).state();
    NotificationManager manager = getSystemService(NotificationManager.class);
    if (manager != null) {
      manager.notify(NOTIFICATION_ID, buildNotification(
          state.message.isEmpty() ? "准备就绪" : state.message));
    }
    if (!state.running) {
      // 备份结束（完成/停止/失败）就撤掉常驻通知并退出服务，别让用户看到一个不动的通知
      handler.removeCallbacks(poll);
      BackupController.get(this).removeListener(listener);
      stopForeground(true);
      stopSelf();
    }
  }

  private Notification buildNotification(String text) {
    return new Notification.Builder(this, CHANNEL_ID)
        .setContentTitle("正在备份相册到电脑")
        .setContentText(text)
        .setStyle(new Notification.BigTextStyle().bigText(text))
        .setSmallIcon(R.drawable.ic_share_notification)
        .setOngoing(true)
        .build();
  }

  private void startForegroundCompat(Notification notification) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
    } else {
      startForeground(NOTIFICATION_ID, notification);
    }
  }

  @Override
  public void onDestroy() {
    handler.removeCallbacks(poll);
    BackupController.get(this).removeListener(listener);
    super.onDestroy();
  }

  @Override
  public IBinder onBind(Intent intent) {
    return null;
  }

  private void createChannel() {
    NotificationManager manager = getSystemService(NotificationManager.class);
    if (manager == null || manager.getNotificationChannel(CHANNEL_ID) != null) {
      return;
    }
    NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "相册备份",
        NotificationManager.IMPORTANCE_LOW);
    channel.setDescription("备份相册时的进度提示");
    manager.createNotificationChannel(channel);
  }
}
