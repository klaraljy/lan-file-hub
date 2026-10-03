package com.lanfilehub.share;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.webkit.CookieManager;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.TextView;
import android.widget.Toast;

/**
 * App 内打开电脑的网页（用户 2026-10-02 选定的方案："就在 App 内打开电脑的网页，简单一点"）。
 *
 * <p>文件列表、上传、下载、分享全部复用电脑端网页，所以这里必须打开 JS 与 DOM storage、
 * 允许 Cookie —— 否则登录状态存不住，用户每次进 App 都要重新登录。
 * （2026-10-03 起网页那边勾了「记住我」会把令牌放 localStorage，配后端 30 天长效令牌，
 *   冷启动也不用重登；DOM storage 打开是这条链路的必要条件。）
 *
 * <p>2026-10-03 用户反馈顶栏"IP 地址 + 刷新 + 浏览器 + 关闭"太突兀，所以顶栏改成一行：
 * 左边固定「局域网文件柜」，中间小字地址，右边三个图标按钮（见 activity_web.xml）。
 *
 * <p>2026-10-03 补两件事（用户反馈"app 里面的 apk 下载直接改成更新"）：
 * <ol>
 *   <li><b>下载必须真的能下</b>：WebView 默认**没有**下载能力，点到下载链接毫无反应。
 *       这里装一个 {@code DownloadListener}，交给系统 DownloadManager 下到「下载」目录，
 *       通知栏点一下就能装（APK）或打开；</li>
 *   <li><b>让网页知道装的是哪个构建号</b>：UA 里加一段 {@code LanFileHubApp/<versionCode>}，
 *       网页据此把按钮显示成「更新」并比较版本（见 frontend 的 appReleaseApi）。</li>
 * </ol>
 */
public class WebViewActivity extends Activity {

  public static final String EXTRA_URL = "extra_url";
  public static final String EXTRA_TITLE = "extra_title";

  /** UA 标记：网页靠它认出"我在 App 里"，并读出已装的构建号 */
  public static final String UA_MARKER = "LanFileHubApp/";

  private WebView webView;
  private String url;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_web);

    url = getIntent().getStringExtra(EXTRA_URL);
    String title = getIntent().getStringExtra(EXTRA_TITLE);
    TextView titleView = findViewById(R.id.web_title);
    // 标题固定是应用名（用户要求：IP 不要当标题占一整行），地址放到旁边的小字里
    titleView.setText(title == null || title.isEmpty() ? getString(R.string.app_name) : title);
    TextView addressView = findViewById(R.id.web_address);
    addressView.setText(hostOf(url));

    webView = findViewById(R.id.web_view);
    WebSettings settings = webView.getSettings();
    settings.setJavaScriptEnabled(true);
    settings.setDomStorageEnabled(true);
    settings.setDatabaseEnabled(true);
    settings.setLoadWithOverviewMode(true);
    settings.setUseWideViewPort(true);
    // 手机访问的是局域网明文 HTTP（没有证书），必须允许；只在 App 内部用
    settings.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
    // 让网页知道"我在 App 里、装的是构建号几"（网页据此显示「更新」并比较版本）
    settings.setUserAgentString(settings.getUserAgentString() + " " + UA_MARKER + versionCode());

    CookieManager cookies = CookieManager.getInstance();
    cookies.setAcceptCookie(true);
    cookies.setAcceptThirdPartyCookies(webView, true);

    // 保留 Cookie 与 DOM storage（含 localStorage）到磁盘：App 冷启动后登录态还在
    CookieManager.getInstance().flush();

    // 不跳系统浏览器：所有链接都留在 App 里（用户要的就是"App 内打开"）
    webView.setWebViewClient(new WebViewClient());
    webView.setWebChromeClient(new WebChromeClient());

    // 下载（导出的文件、安装包）：WebView 自己不会下载，必须交给系统 DownloadManager，
    // 否则用户点「下载」「更新」毫无反应（2026-10-03 用户实测反馈）
    webView.setDownloadListener(this::startSystemDownload);

    if (url != null) {
      webView.loadUrl(url);
    } else {
      finish();
      return;
    }

    TextView reload = findViewById(R.id.web_reload);
    TextView external = findViewById(R.id.web_external);
    TextView close = findViewById(R.id.web_close);
    reload.setOnClickListener(view -> webView.reload());
    // 地址那一小段也能点：点它 = 刷新（用户习惯：标题栏点一下刷新）
    addressView.setOnClickListener(view -> webView.reload());
    external.setOnClickListener(
        view -> {
          Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
          startActivity(intent);
        });
    close.setOnClickListener(view -> finish());
  }

  /**
   * 把下载交给系统 DownloadManager：
   * 用服务器给的 Content-Disposition 文件名落到「下载」目录，完成时通知栏点一下就能安装/打开。
   * 带上 Cookie：登录态下载也走这条路（带票据的地址不需要，但带上无害）。
   */
  private void startSystemDownload(String downloadUrl, String userAgent, String contentDisposition,
                                   String mimeType, long contentLength) {
    if (downloadUrl == null || downloadUrl.startsWith("blob:") || downloadUrl.startsWith("data:")) {
      // blob:/data: 交给网页自己处理（DownloadManager 拿不到内容）
      Toast.makeText(this, getString(R.string.web_download_unsupported), Toast.LENGTH_LONG).show();
      return;
    }
    try {
      android.app.DownloadManager.Request request =
          new android.app.DownloadManager.Request(Uri.parse(downloadUrl));
      request.setMimeType(mimeType);
      request.setNotificationVisibility(
          android.app.DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
      String cookie = CookieManager.getInstance().getCookie(downloadUrl);
      if (cookie != null && !cookie.isEmpty()) {
        request.addRequestHeader("Cookie", cookie);
      }
      // 服务器给了文件名就用它（后端就是按原始文件名发的 Content-Disposition）
      String fileName = DownloadNaming.fromContentDisposition(contentDisposition);
      if (fileName != null) {
        request.setTitle(fileName);
        request.setDestinationInExternalPublicDir(
            android.os.Environment.DIRECTORY_DOWNLOADS, fileName);
      } else {
        request.setTitle(DownloadNaming.fromUrl(downloadUrl));
      }
      android.app.DownloadManager manager =
          (android.app.DownloadManager) getSystemService(DOWNLOAD_SERVICE);
      if (manager == null) {
        Toast.makeText(this, getString(R.string.web_download_failed), Toast.LENGTH_LONG).show();
        return;
      }
      manager.enqueue(request);
      Toast.makeText(this, getString(R.string.web_download_started), Toast.LENGTH_LONG).show();
    } catch (Exception error) {
      Toast.makeText(this, getString(R.string.web_download_failed), Toast.LENGTH_LONG).show();
    }
  }

  /** 本 App 的 versionCode（UA 标记用它，与「连接」页显示的构建号是同一个数） */
  private long versionCode() {
    try {
      android.content.pm.PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
      if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
        return info.getLongVersionCode();
      }
      return info.versionCode;
    } catch (Exception error) {
      return 0;
    }
  }

  /** 顶栏那行小字：只显示 host:port，太长时中间省略（URL 的其它部分对用户没意义） */
  private static String hostOf(String url) {    if (url == null) {
      return "";
    }
    String text = url;
    if (text.startsWith("http://")) {
      text = text.substring("http://".length());
    } else if (text.startsWith("https://")) {
      text = text.substring("https://".length());
    }
    final int slash = text.indexOf('/');
    if (slash >= 0) {
      text = text.substring(0, slash);
    }
    return text;
  }

  @Override
  protected void onPause() {
    super.onPause();
    // 退出前台时把 Cookie 落盘（老机器的 WebView 不一定及时刷）
    CookieManager.getInstance().flush();
  }

  @Override
  public void onBackPressed() {
    if (webView != null && webView.canGoBack()) {
      webView.goBack();
      return;
    }
    super.onBackPressed();
  }

  @Override
  protected void onDestroy() {
    if (webView != null) {
      webView.setVisibility(View.GONE);
      webView.destroy();
    }
    super.onDestroy();
  }
}
