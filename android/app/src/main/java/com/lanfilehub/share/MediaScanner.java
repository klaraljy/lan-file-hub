package com.lanfilehub.share;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.MediaStore;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * 读手机相册（2026-10-03，给「相册备份」用）。
 *
 * <p>只读**图片**（{@code MediaStore.Images}）：视频体积大、数量少，用户更需要的是照片先有备份；
 * 而且一次读进内存的上传方式不适合视频（见 {@link ApiClient} 的说明）。
 *
 * <p>按"相册"（MediaStore 里的 BUCKET_DISPLAY_NAME，即图片所在文件夹名）分组，
 * 让用户能勾"只备份相机"或"只备份微信保存的图"。
 */
public final class MediaScanner {

  private MediaScanner() {}

  /** 一个相册：名字 + 里面有多少张图 */
  public static final class Bucket {
    public final String name;
    public final int count;

    public Bucket(String name, int count) {
      this.name = name;
      this.count = count;
    }

    @Override
    public String toString() {
      return name + "（" + count + " 张）";
    }
  }

  /** 列出所有相册（按名字排序；权限不足时返回空列表，界面会提示先授权） */
  public static List<Bucket> buckets(Context context) {
    Map<String, Integer> counts = new LinkedHashMap<>();
    Cursor cursor = null;
    try {
      cursor = context.getContentResolver().query(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
          new String[] {MediaStore.Images.Media.BUCKET_DISPLAY_NAME}, null, null,
          MediaStore.Images.Media.BUCKET_DISPLAY_NAME + " ASC");
      if (cursor == null) {
        return new ArrayList<>();
      }
      final int column = cursor.getColumnIndex(MediaStore.Images.Media.BUCKET_DISPLAY_NAME);
      while (cursor.moveToNext()) {
        String name = column >= 0 ? cursor.getString(column) : null;
        if (name == null || name.isEmpty()) {
          name = "其它";
        }
        Integer previous = counts.get(name);
        counts.put(name, previous == null ? 1 : previous + 1);
      }
    } catch (SecurityException denied) {
      return new ArrayList<>();
    } finally {
      if (cursor != null) {
        cursor.close();
      }
    }

    List<Bucket> result = new ArrayList<>();
    for (Map.Entry<String, Integer> entry : counts.entrySet()) {
      result.add(new Bucket(entry.getKey(), entry.getValue()));
    }
    return result;
  }

  /** 列出指定相册里的照片（按拍摄时间从早到晚：先传旧的，用户能看到"进度在往前走"） */
  public static List<BackupPlanner.Photo> photos(Context context, String bucket) {
    List<BackupPlanner.Photo> photos = new ArrayList<>();
    Cursor cursor = null;
    try {
      cursor = context.getContentResolver().query(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
          new String[] {
              MediaStore.Images.Media._ID,
              MediaStore.Images.Media.BUCKET_DISPLAY_NAME,
              MediaStore.Images.Media.DISPLAY_NAME,
              MediaStore.Images.Media.SIZE,
              MediaStore.Images.Media.DATE_TAKEN,
          },
          MediaStore.Images.Media.BUCKET_DISPLAY_NAME + " = ?", new String[] {bucket},
          MediaStore.Images.Media.DATE_TAKEN + " ASC");
      if (cursor == null) {
        return photos;
      }
      final int idColumn = cursor.getColumnIndex(MediaStore.Images.Media._ID);
      final int bucketColumn = cursor.getColumnIndex(MediaStore.Images.Media.BUCKET_DISPLAY_NAME);
      final int nameColumn = cursor.getColumnIndex(MediaStore.Images.Media.DISPLAY_NAME);
      final int sizeColumn = cursor.getColumnIndex(MediaStore.Images.Media.SIZE);
      final int dateColumn = cursor.getColumnIndex(MediaStore.Images.Media.DATE_TAKEN);
      while (cursor.moveToNext()) {
        long dateTaken = dateColumn >= 0 && !cursor.isNull(dateColumn) ? cursor.getLong(dateColumn)
                                                                      : 0L;
        if (dateTaken <= 0 && dateColumn >= 0) {
          // 有些机型 DATE_TAKEN 为空：退化成"没有时间"，键里仍然是 0（不影响去重）
          dateTaken = 0L;
        }
        photos.add(new BackupPlanner.Photo(idColumn >= 0 ? cursor.getLong(idColumn) : 0L,
            bucketColumn >= 0 ? cursor.getString(bucketColumn) : bucket,
            nameColumn >= 0 ? cursor.getString(nameColumn) : "",
            sizeColumn >= 0 ? cursor.getLong(sizeColumn) : 0L, dateTaken));
      }
    } catch (SecurityException denied) {
      return photos;
    } finally {
      if (cursor != null) {
        cursor.close();
      }
    }
    return photos;
  }

  /** 按 id 打开一张照片的内容流（上传时用）；读不到返回 null，调用方按"这张失败"处理 */
  public static java.io.InputStream open(Context context, long photoId) {
    ContentResolver resolver = context.getContentResolver();
    Uri uri = Uri.withAppendedPath(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
        String.valueOf(photoId));
    try {
      return resolver.openInputStream(uri);
    } catch (java.io.FileNotFoundException missing) {
      return null;
    }
  }
}
