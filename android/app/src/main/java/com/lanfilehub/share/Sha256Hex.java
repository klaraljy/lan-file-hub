package com.lanfilehub.share;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

/**
 * SHA-256 十六进制（小写）。
 *
 * <p>为什么自己实现：{@code java.security.MessageDigest} 是 JDK/Android 自带的，
 * 没必要引第三方库；上传接口的 {@code sha256} 声明必须是 64 位小写十六进制。
 */
public final class Sha256Hex {

  private static final char[] HEX = "0123456789abcdef".toCharArray();

  private Sha256Hex() {}

  public static String of(byte[] data) {
    try {
      MessageDigest digest = MessageDigest.getInstance("SHA-256");
      byte[] hash = digest.digest(data);
      StringBuilder builder = new StringBuilder(hash.length * 2);
      for (byte value : hash) {
        builder.append(HEX[(value >> 4) & 0x0F]).append(HEX[value & 0x0F]);
      }
      return builder.toString().toLowerCase(Locale.ROOT);
    } catch (NoSuchAlgorithmException error) {
      // 任何 Android/JVM 都有 SHA-256；真没有就只能不声明哈希（后端会自己算）
      return "";
    }
  }
}
