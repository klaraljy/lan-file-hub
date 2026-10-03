package com.lanfilehub.share;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * 相册备份的"该传哪些"决策（用户 2026-10-03 批准的功能之一）。
 *
 * <p>刻意做成**纯逻辑**：不碰 MediaStore、不碰网络、不碰 SharedPreferences，
 * 所以能在电脑上跑单测（{@code BackupPlannerTest}）。真机上的相册读取与上传分别由
 * {@link MediaScanner} 与 {@link BackupService} 负责。
 *
 * <p>去重规则：每条已备份记录用 {@link #photoKey} 当键 —— 相册名 + 文件名 + 字节数 +
 * 拍摄时间。为什么不用"文件名"一个字段：不同相册会有同名照片（IMG_0001.jpg 到处都是），
 * 而加了大小与时间之后，同一张照片重命名也会被认出来是新的（宁可再传一次，也不要漏传）。
 */
public final class BackupPlanner {

  private BackupPlanner() {}

  /** 一张待备份的照片（从 MediaStore 读出来的最小信息） */
  public static final class Photo {
    public final long id;
    public final String bucket;
    public final String displayName;
    public final long sizeBytes;
    public final long dateTakenMillis;

    public Photo(long id, String bucket, String displayName, long sizeBytes, long dateTakenMillis) {
      this.id = id;
      this.bucket = bucket == null ? "" : bucket;
      this.displayName = displayName == null ? "" : displayName;
      this.sizeBytes = sizeBytes;
      this.dateTakenMillis = dateTakenMillis;
    }

    @Override
    public String toString() {
      return bucket + "/" + displayName;
    }
  }

  /** 一条已备份记录（存进 SharedPreferences，见 BackupStore） */
  public static String photoKey(Photo photo) {
    return photo.bucket + "\u0000" + photo.displayName + "\u0000" + photo.sizeBytes + "\u0000"
        + photo.dateTakenMillis;
  }

  /** 便于调用方少写一层（BackupController 用它攒批量写盘） */
  public static String keyOf(Photo photo) {
    return photoKey(photo);
  }

  /** 哪些照片还没备份过（保持传入顺序，方便按时间顺序上传） */
  public static List<Photo> pending(List<Photo> photos, Set<String> backedUpKeys) {
    List<Photo> result = new ArrayList<>();
    if (photos == null) {
      return result;
    }
    Set<String> done = backedUpKeys == null ? new HashSet<>() : backedUpKeys;
    for (Photo photo : photos) {
      if (!done.contains(photoKey(photo))) {
        result.add(photo);
      }
    }
    return result;
  }

  /**
   * 上传到服务器时用的文件名。
   *
   * <p>为什么加"日期_相册"前缀：相册里全是 `IMG_20261003_120000.jpg` 这类名字，
   * 不同相册、不同手机的都会撞名；服务器遇到同名会自动加 (1)、(2)（用户 2026-10-03 的要求），
   * 结果就是一堆 `IMG_0001 (7).jpg` —— 用户根本认不出哪张是哪张。加了前缀至少能看出
   * "哪个相册、哪一天拍的"。
   */
  public static String remoteName(Photo photo) {
    String day = formatDay(photo.dateTakenMillis);
    String bucket = sanitize(photo.bucket);
    String name = sanitize(photo.displayName);
    if (name.isEmpty()) {
      name = "photo_" + photo.id + ".jpg";
    }
    if (bucket.isEmpty()) {
      return day + "_" + name;
    }
    return day + "_" + bucket + "_" + name;
  }

  /** 文件名里不能出现的字符（服务器也会净化，这里先自己收拾干净） */
  static String sanitize(String raw) {
    if (raw == null) {
      return "";
    }
    StringBuilder builder = new StringBuilder(raw.length());
    for (char ch : raw.toCharArray()) {
      boolean bad = ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"'
          || ch == '<' || ch == '>' || ch == '|' || ch < 0x20;
      builder.append(bad ? '_' : ch);
    }
    return builder.toString().trim();
  }

  /** 拍摄日期（本地时区）→ `20261003`；拿不到时间就用 0 → `00000000` */
  static String formatDay(long millis) {
    if (millis <= 0) {
      return "00000000";
    }
    java.util.Calendar calendar = java.util.Calendar.getInstance();
    calendar.setTimeInMillis(millis);
    return String.format(Locale.ROOT, "%04d%02d%02d", calendar.get(java.util.Calendar.YEAR),
        calendar.get(java.util.Calendar.MONTH) + 1, calendar.get(java.util.Calendar.DAY_OF_MONTH));
  }

  /** 进度文案：给用户看的是"第几个/共几个"，不是字节数 */
  public static String progressText(int index, int total) {
    return "正在备份 " + (index + 1) + "/" + total;
  }
}
