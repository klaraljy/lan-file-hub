package com.lanfilehub.share;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.IOException;

/**
 * 分享服务的全局状态（单例）。
 *
 * <p>为什么用单例：服务对象要被「界面」和「前台服务」同时看到 —— 界面负责显示地址与二维码，
 * 前台服务负责让进程活着（避免分享过程中被系统回收）。把服务器放在进程内的单例里，
 * 前台服务只要在运行，进程就是高优先级，服务器线程与它管理的文件都不会被清掉。
 * 这也是"最小可行"的做法：不上 AIDL、不做 bindService。
 */
public final class ShareController {

  private static final String TAG = "LanShare";
  /** 先试 8080；被占用时 ShareHttpServer 会自动改用系统端口 */
  private static final int PREFERRED_PORT = 8080;

  private static ShareHttpServer server;

  private ShareController() {
  }

  /** 分享目录：App 外部私有目录，卸载即清理，不需要任何存储权限 */
  public static synchronized File shareDir(Context context) {
    File dir = new File(context.getExternalFilesDir(null), "share");
    if (!dir.exists() && !dir.mkdirs()) {
      Log.w(TAG, "创建分享目录失败: " + dir.getAbsolutePath());
    }
    return dir;
  }

  public static synchronized ShareHttpServer start(Context context) throws IOException {
    if (server != null && server.isRunning()) {
      return server;
    }
    server = new ShareHttpServer(shareDir(context), PREFERRED_PORT, message -> Log.i(TAG, message));
    server.start();
    return server;
  }

  public static synchronized void stop() {
    if (server != null) {
      server.stop();
    }
  }

  public static synchronized boolean isRunning() {
    return server != null && server.isRunning();
  }

  public static synchronized ShareHttpServer current() {
    return server;
  }

  /** 分享地址：形如 http://192.168.43.1:8080/ ；拿不到 IP 时返回 null。 */
  public static synchronized String shareUrl() {
    String ip = NetUtil.lanIp();
    if (ip == null || server == null || !server.isRunning()) {
      return null;
    }
    return "http://" + ip + ":" + server.getPort() + "/";
  }
}
