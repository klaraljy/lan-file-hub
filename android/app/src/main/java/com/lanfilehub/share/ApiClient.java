package com.lanfilehub.share;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * 后端接口的最小客户端（2026-10-03，给「相册备份」用）。
 *
 * <p>为什么自己写而不是引第三方库：这个 App 刻意**不引 AndroidX、不引 OkHttp**（依赖少、
 * 构建快、老手机不挑），而后端接口只用到 6 个调用。JSON 用系统自带的 {@code org.json}。
 *
 * <p>⛔ 令牌只存在 SharedPreferences（{@link BackupStore}），**不写日志、不进通知文案**。
 * <p>⛔ 每个请求都有连接/读取超时（局域网里服务没开时不能挂死）。
 */
public final class ApiClient {

  private static final int CONNECT_TIMEOUT_MS = 3000;
  private static final int READ_TIMEOUT_MS = 15000;
  /** 分块大小：与后端默认一致（8 MiB）；照片一般一块就够，视频才会多块 */
  private static final int CHUNK_SIZE = 8 * 1024 * 1024;

  private final String baseUrl;
  private final String token;

  public ApiClient(String host, int port, String token) {
    this.baseUrl = "http://" + host + ":" + port + "/api/v1";
    this.token = token == null ? "" : token;
  }

  /** 登录：成功返回令牌（记得同时把用户名存下来给界面显示） */
  public static String login(String host, int port, String username, String password)
      throws IOException, JSONException {
    JSONObject body = new JSONObject();
    body.put(username.contains("@") ? "email" : "username", username);
    body.put("password", password);
    // 「记住我」：手机上不该天天重新登录（用户 2026-10-03 的要求）
    body.put("remember", true);
    JSONObject response = post(host, port, "/auth/login", body, null);
    return response.getJSONObject("data").getString("access_token");
  }

  /** 根目录 id（后端第一次访问会自动建） */
  public String rootFolderId() throws IOException, JSONException {
    return get("/folders/root").getJSONObject("data").getString("id");
  }

  /**
   * 确保根目录下有一个叫 {@code name} 的文件夹，返回它的 id。
   *
   * <p>为什么先查再建：后端对同名文件夹返回 409（文件夹不像文件那样自动改名），
   * 所以"查不到才建"是这里的正常路径。
   */
  public String ensureFolder(String parentId, String name) throws IOException, JSONException {
    JSONObject listing = get("/folders/" + parentId + "/children?page_size=100");
    org.json.JSONArray items = listing.getJSONObject("data").getJSONArray("items");
    for (int index = 0; index < items.length(); index++) {
      JSONObject folder = items.getJSONObject(index);
      if (name.equals(folder.optString("name"))) {
        return folder.getString("id");
      }
    }
    JSONObject body = new JSONObject();
    body.put("parent_id", parentId);
    body.put("name", name);
    return request("POST", baseUrl, "/folders", body, token).getJSONObject("data")
        .getString("id");
  }

  /** 上传一个文件（自动分块）。返回服务器上的文件名（同名时后端会加 (1)、(2)…）。 */
  public String upload(String folderId, String fileName, String mimeType, byte[] content)
      throws IOException, JSONException {
    JSONObject create = new JSONObject();
    create.put("folder_id", folderId);
    create.put("file_name", fileName);
    create.put("mime_type", mimeType == null || mimeType.isEmpty() ? "application/octet-stream"
        : mimeType);
    create.put("size_bytes", content.length);
    create.put("chunk_size", CHUNK_SIZE);
    create.put("sha256", Sha256Hex.of(content));

    JSONObject session = request("POST", baseUrl, "/uploads", create, token).getJSONObject("data");
    String uploadId = session.getString("upload_id");
    int totalChunks = session.getInt("total_chunks");

    for (int index = 0; index < totalChunks; index++) {
      int offset = index * CHUNK_SIZE;
      int length = Math.min(CHUNK_SIZE, content.length - offset);
      byte[] part = new byte[length];
      System.arraycopy(content, offset, part, 0, length);
      putChunk(uploadId, index, part);
    }

    JSONObject done = request("POST", baseUrl, "/uploads/" + uploadId + "/complete",
        new JSONObject(), token).getJSONObject("data");
    return done.optString("name", fileName);
  }

  private void putChunk(String uploadId, int index, byte[] part) throws IOException, JSONException {
    HttpURLConnection connection = null;
    try {
      URL url = new URL(baseUrl + "/uploads/" + uploadId + "/chunks/" + index);
      connection = (HttpURLConnection) url.openConnection();
      connection.setRequestMethod("PUT");
      connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
      connection.setReadTimeout(READ_TIMEOUT_MS);
      connection.setDoOutput(true);
      connection.setRequestProperty("Content-Type", "application/octet-stream");
      connection.setRequestProperty("X-Chunk-Sha256", Sha256Hex.of(part));
      if (!token.isEmpty()) {
        connection.setRequestProperty("Authorization", "Bearer " + token);
      }
      try (OutputStream out = connection.getOutputStream()) {
        out.write(part);
      }
      readBody(connection);
    } finally {
      if (connection != null) {
        connection.disconnect();
      }
    }
  }

  private JSONObject get(String path) throws IOException, JSONException {
    return request("GET", baseUrl, path, null, token);
  }

  private static JSONObject post(String host, int port, String path, JSONObject body, String token)
      throws IOException, JSONException {
    return request("POST", "http://" + host + ":" + port + "/api/v1", path, body, token);
  }

  private static JSONObject request(String method, String base, String path, JSONObject body,
                                    String token) throws IOException, JSONException {
    HttpURLConnection connection = null;
    try {
      URL url = new URL(base + path);
      connection = (HttpURLConnection) url.openConnection();
      connection.setRequestMethod(method);
      connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
      connection.setReadTimeout(READ_TIMEOUT_MS);
      if (!token.isEmpty()) {
        connection.setRequestProperty("Authorization", "Bearer " + token);
      }
      if (body != null) {
        connection.setDoOutput(true);
        connection.setRequestProperty("Content-Type", "application/json");
        try (OutputStream out = connection.getOutputStream()) {
          out.write(body.toString().getBytes(StandardCharsets.UTF_8));
        }
      }
      String text = readBody(connection);
      int code = connection.getResponseCode();
      if (code < 200 || code >= 300) {
        // 把后端的 code 带出来，界面上才能给"为什么失败"（401 = 登录过期，409 = 冲突…）
        String apiCode = "";
        try {
          apiCode = new JSONObject(text).getJSONObject("error").optString("code", "");
        } catch (JSONException ignored) {
          // 非 JSON 响应（例如 502）就只留 HTTP 码
        }
        throw new IOException("HTTP " + code + (apiCode.isEmpty() ? "" : " " + apiCode));
      }
      return new JSONObject(text);
    } finally {
      if (connection != null) {
        connection.disconnect();
      }
    }
  }

  private static String readBody(HttpURLConnection connection) throws IOException {
    InputStream stream = connection.getResponseCode() >= 400 ? connection.getErrorStream()
                                                             : connection.getInputStream();
    if (stream == null) {
      return "";
    }
    StringBuilder builder = new StringBuilder();
    try (BufferedReader reader =
             new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))) {
      String line;
      while ((line = reader.readLine()) != null) {
        builder.append(line);
      }
    }
    return builder.toString();
  }

  /** 流式读文件内容（照片几 MB，进内存没问题；视频才有风险，所以这里只备份图片） */
  static byte[] readAll(InputStream stream) throws IOException {
    ByteArrayOutputStream buffer = new ByteArrayOutputStream();
    byte[] chunk = new byte[64 * 1024];
    int read;
    while ((read = stream.read(chunk)) > 0) {
      buffer.write(chunk, 0, read);
    }
    return buffer.toByteArray();
  }
}
