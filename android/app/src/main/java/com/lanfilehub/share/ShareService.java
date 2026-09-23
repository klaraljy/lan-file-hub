package com.lanfilehub.share;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * 前台服务：唯一职责是**让进程活着**，这样分享过程中切到后台/锁屏也不会被系统回收。
 *
 * <p>HTTP 服务本身在 {@link ShareController} 的单例里跑（见那里的说明），
 * 这里只挂一个常驻通知并显示当前地址，用户一眼能看到"还在分享"。
 */
public class ShareService extends Service {

  private static final String CHANNEL_ID = "lan-share";
  private static final int NOTIFICATION_ID = 1001;

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    createChannel();
    String url = ShareController.shareUrl();
    Notification.Builder builder = new Notification.Builder(this, CHANNEL_ID)
        .setContentTitle("局域网快传正在分享")
        .setContentText(url == null ? "请确认已连接 WiFi 或已开启热点" : url)
        .setSmallIcon(R.drawable.ic_share_notification)
        .setOngoing(true);
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      startForeground(NOTIFICATION_ID, builder.build(),
          ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
    } else {
      startForeground(NOTIFICATION_ID, builder.build());
    }
    return START_STICKY;
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
    NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "文件分享",
        NotificationManager.IMPORTANCE_LOW);
    channel.setDescription("分享进行中的常驻提示");
    manager.createNotificationChannel(channel);
  }
}
