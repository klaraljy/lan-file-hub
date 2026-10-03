package com.lanfilehub.share;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * 相册备份的"该传哪些"逻辑（2026-10-03）——纯函数，在电脑上就能跑。
 *
 * <p>钉两件事：① 传过的绝不再传（用户最在意的"别重复占空间"）；
 * ② 服务器上的文件名要能看出"哪个相册、哪一天"（否则一堆 IMG_0001 (7).jpg 谁也认不出）。
 * 真机上的相册读取与上传分别由 MediaScanner / BackupService 负责，需要手机才能验。
 */
public class BackupPlannerTest {

  private static BackupPlanner.Photo photo(String bucket, String name, long size, long date) {
    return new BackupPlanner.Photo(1L, bucket, name, size, date);
  }

  @Test
  public void pendingKeepsOnlyPhotosThatWereNotBackedUpYet() {
    BackupPlanner.Photo first = photo("Camera", "IMG_0001.jpg", 100, 1000);
    BackupPlanner.Photo second = photo("Camera", "IMG_0002.jpg", 200, 2000);
    BackupPlanner.Photo third = photo("WeChat", "IMG_0001.jpg", 100, 1000);

    Set<String> done = new HashSet<>();
    done.add(BackupPlanner.photoKey(first));

    List<BackupPlanner.Photo> pending =
        BackupPlanner.pending(Arrays.asList(first, second, third), done);

    assertEquals(2, pending.size());
    assertEquals("IMG_0002.jpg", pending.get(0).displayName);
    // 不同相册的同名同大小照片是**两张不同的照片**，不能因为名字一样就跳过
    assertEquals("WeChat", pending.get(1).bucket);
  }

  @Test
  public void sameNameButDifferentSizeOrTimeCountsAsNew() {
    BackupPlanner.Photo old = photo("Camera", "IMG_0001.jpg", 100, 1000);
    Set<String> done = new HashSet<>(Collections.singletonList(BackupPlanner.photoKey(old)));

    // 相机重新拍了一张同名照片（大小或时间不同）→ 必须当成新照片
    List<BackupPlanner.Photo> changed =
        BackupPlanner.pending(Collections.singletonList(photo("Camera", "IMG_0001.jpg", 111, 1000)),
            done);
    assertEquals(1, changed.size());

    List<BackupPlanner.Photo> otherTime =
        BackupPlanner.pending(Collections.singletonList(photo("Camera", "IMG_0001.jpg", 100, 1001)),
            done);
    assertEquals(1, otherTime.size());

    // 完全一样的那张 → 跳过
    assertTrue(BackupPlanner.pending(Collections.singletonList(old), done).isEmpty());
  }

  @Test
  public void remoteNameCarriesDayAndBucket() {
    String name = BackupPlanner.remoteName(photo("Camera", "IMG_0001.jpg", 100, 0));
    assertTrue(name.startsWith("00000000_Camera_"));
    assertTrue(name.endsWith("IMG_0001.jpg"));

    // 有拍摄时间时用日期（本地时区），方便用户按天翻
    java.util.Calendar calendar = java.util.Calendar.getInstance();
    calendar.set(2026, java.util.Calendar.OCTOBER, 3, 12, 0, 0);
    String dated = BackupPlanner.remoteName(
        photo("WeChat", "mmexport123.jpg", 100, calendar.getTimeInMillis()));
    assertEquals("20261003_WeChat_mmexport123.jpg", dated);
  }

  @Test
  public void remoteNameSanitisesPathCharacters() {
    String name = BackupPlanner.remoteName(photo("a/b", "..\\evil:name?.jpg", 1, 0));
    assertFalse(name.contains("/"));
    assertFalse(name.contains("\\"));
    assertFalse(name.contains(":"));
    assertFalse(name.contains("?"));
  }

  @Test
  public void remoteNameStillWorksWithoutNameOrBucket() {
    String name = BackupPlanner.remoteName(new BackupPlanner.Photo(7L, null, "", 0, 0));
    assertEquals("00000000_photo_7.jpg", name);
  }

  @Test
  public void nullInputsAreHandledInsteadOfCrashing() {
    assertTrue(BackupPlanner.pending(null, null).isEmpty());
    assertTrue(BackupPlanner.pending(Collections.<BackupPlanner.Photo>emptyList(), null).isEmpty());
  }
}
