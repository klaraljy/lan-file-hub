package com.lanfilehub.share;

import android.content.Context;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * 相册备份的执行器（2026-10-03）：跑一个后台线程，逐张把照片传到电脑。
 *
 * <p>为什么做成单例（与 {@link ShareController} 同一个思路）：界面（{@link BackupActivity}）
 * 和前台服务（{@link BackupService}）都要看同一份进度，而备份过程不该因为用户切页就中断。
 *
 * <p>用户口径（2026-10-03 批准的功能）：
 *   - **只在 WiFi / 热点下备份**（默认开，可关）：移动数据下自动停下并说明原因；
 *   - 备份到电脑上的「手机相册」文件夹（不存在就建）；
 *   - 传过的照片**不会重复传**（清单在本地，见 {@link BackupStore}）；
 *   - 中途停下（用户点停、网络变了、App 被杀）下次接着传，已经传好的不会重来。
 */
public final class BackupController {

  /** 备份目标文件夹在电脑上的名字（用户在网页里能一眼认出来） */
  public static final String REMOTE_FOLDER = "手机相册";

  /** 给界面看的进度快照 */
  public static final class State {
    public boolean running;
    public int total;
    public int done;
    public int uploaded;
    public int failed;
    public String message = "";
    /** 最近一次失败的原因（界面上显示，别只写"失败"） */
    public String lastError = "";
  }

  public interface Listener {
    void onState(State state);
  }

  private static BackupController instance;

  private final Context appContext;
  private final State state = new State();
  private final AtomicBoolean stopRequested = new AtomicBoolean(false);
  private final List<Listener> listeners = new ArrayList<>();
  private Thread worker;

  private BackupController(Context context) {
    this.appContext = context.getApplicationContext();
  }

  public static synchronized BackupController get(Context context) {
    if (instance == null) {
      instance = new BackupController(context);
    }
    return instance;
  }

  public synchronized State state() {
    State copy = new State();
    copy.running = state.running;
    copy.total = state.total;
    copy.done = state.done;
    copy.uploaded = state.uploaded;
    copy.failed = state.failed;
    copy.message = state.message;
    copy.lastError = state.lastError;
    return copy;
  }

  public synchronized void addListener(Listener listener) {
    listeners.add(listener);
    listener.onState(state());
  }

  public synchronized void removeListener(Listener listener) {
    listeners.remove(listener);
  }

  public boolean isRunning() {
    return worker != null && worker.isAlive();
  }

  /**
   * 收集"还没备份过"的照片（选中的相册 × 本地清单）。
   *
   * <p>⛔ 预览与实际备份**必须用同一个方法**：用户看到的"将上传 128 张"要是真实清单，
   * 不能预览一套、实际传另一套（那正是用户担心的"点一下就全备份了"）。
   */
  public static List<BackupPlanner.Photo> collectPending(Context context) {
    Set<String> buckets = BackupStore.buckets(context);
    List<BackupPlanner.Photo> pending = new ArrayList<>();
    if (buckets.isEmpty()) {
      return pending;
    }
    Set<String> backedUp = BackupStore.backedUpKeys(context);
    for (String bucket : buckets) {
      pending.addAll(BackupPlanner.pending(MediaScanner.photos(context, bucket), backedUp));
    }
    return pending;
  }

  /** 开工前的预览（数量 + 体积 + 现在能不能传），供界面弹确认框用 */
  public static BackupPlanner.Summary preview(Context context) {
    return BackupPlanner.summarize(collectPending(context));
  }

  /** 现在能不能传（仅 WiFi 设置 + 当前网络） */
  public static boolean canUploadNow(Context context) {
    if (!BackupStore.wifiOnly(context)) {
      return true;
    }
    return NetworkState.canReachComputer(NetworkState.detect(context));
  }

  /** 开始备份（已在跑就忽略）。scan 在后台线程里做，界面不会卡。 */
  public void start() {
    if (isRunning()) {
      return;
    }
    stopRequested.set(false);
    worker = new Thread(this::runBackup, "photo-backup");
    worker.start();
  }

  public void stop() {
    stopRequested.set(true);
    synchronized (this) {
      state.message = "正在停下…";
    }
    notifyListeners();
  }

  private void runBackup() {
    synchronized (this) {
      state.running = true;
      state.total = 0;
      state.done = 0;
      state.uploaded = 0;
      state.failed = 0;
      state.lastError = "";
      state.message = "正在读取相册…";
    }
    notifyListeners();

    try {
      if (!BackupStore.isConfigured(appContext)) {
        finish("还没连上电脑：先在「连接电脑」里连一次，并在备份页登录");
        return;
      }
      Set<String> buckets = BackupStore.buckets(appContext);
      if (buckets.isEmpty()) {
        finish("还没有选要备份的相册");
        return;
      }
      if (BackupStore.wifiOnly(appContext) && !onWifi()) {
        finish("现在不是 WiFi/热点，已按设置停下（可以关掉「只在 WiFi 下备份」再试）");
        return;
      }

      // 1) 收集待备份清单（与预览用的是同一个方法，用户看到多少就传多少）
      List<BackupPlanner.Photo> pending = collectPending(appContext);
      if (pending.isEmpty()) {
        finish("没有新照片要备份（都传过了）");
        return;
      }
      synchronized (this) {
        state.total = pending.size();
        state.message = "共 " + pending.size() + " 张待备份";
      }
      notifyListeners();

      // 2) 准备服务器侧：登录令牌 + 目标文件夹
      ApiClient api = new ApiClient(BackupStore.host(appContext), BackupStore.port(appContext),
          BackupStore.token(appContext));
      String rootId = api.rootFolderId();
      String folderId = api.ensureFolder(rootId, REMOTE_FOLDER);

      // 3) 逐张上传；每 5 张批量记一次清单（少刷盘，但崩溃最多重传 5 张）
      List<String> finishedKeys = new ArrayList<>();
      for (BackupPlanner.Photo photo : pending) {
        if (stopRequested.get()) {
          flush(finishedKeys);
          finish("已停止（下次接着传，传过的不会重来）");
          return;
        }
        if (BackupStore.wifiOnly(appContext) && !onWifi()) {
          flush(finishedKeys);
          finish("网络从 WiFi 切走了，已停下（已传的不会重传）");
          return;
        }
        synchronized (this) {
          state.message = BackupPlanner.progressText(state.done, state.total) + "：" + photo.displayName;
        }
        notifyListeners();

        try (java.io.InputStream stream = MediaScanner.open(appContext, photo.id)) {
          if (stream == null) {
            throw new java.io.IOException("读不到这张照片");
          }
          byte[] content = ApiClient.readAll(stream);
          api.upload(folderId, BackupPlanner.remoteName(photo), "image/jpeg", content);
          finishedKeys.add(BackupPlanner.keyOf(photo));
          synchronized (this) {
            state.done++;
            state.uploaded++;
          }
          if (finishedKeys.size() >= 5) {
            flush(finishedKeys);
          }
        } catch (Exception error) {
          // 单张失败不中断整批：网络抖一下不该让用户重来一遍
          synchronized (this) {
            state.done++;
            state.failed++;
            state.lastError = photo.displayName + "：" + describe(error);
          }
        }
        notifyListeners();
      }
      flush(finishedKeys);
      finish("备份完成：新传 " + state.uploaded + " 张"
          + (state.failed > 0 ? "，失败 " + state.failed + " 张（下次会重试）" : ""));
    } catch (Exception error) {
      finish("备份失败：" + describe(error));
    }
  }

  private void flush(List<String> keys) {
    if (keys.isEmpty()) {
      return;
    }
    BackupStore.addBackedUpKeys(appContext, new ArrayList<>(keys));
    keys.clear();
  }

  private void finish(String message) {
    synchronized (this) {
      state.running = false;
      state.message = message;
    }
    BackupStore.setLastResult(appContext, message);
    notifyListeners();
  }

  private boolean onWifi() {
    return NetworkState.canReachComputer(NetworkState.detect(appContext));
  }

  /** 把异常翻成用户能懂的一句话（不要把 Java 类名丢给用户看） */
  private static String describe(Exception error) {
    String text = error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage();
    if (text.contains("401")) {
      return "登录过期了，请重新登录";
    }
    if (text.contains("Failed to connect") || text.contains("ECONNREFUSED") || text.contains("timeout")
        || text.contains("timed out")) {
      return "连不上电脑（电脑关机了？或不在同一个网络）";
    }
    return text;
  }

  private void notifyListeners() {
    List<Listener> snapshot;
    synchronized (this) {
      snapshot = new ArrayList<>(listeners);
    }
    State copy = state();
    for (Listener listener : snapshot) {
      listener.onState(copy);
    }
  }
}
