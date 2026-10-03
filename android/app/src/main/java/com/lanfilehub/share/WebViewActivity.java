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
 */
public class WebViewActivity extends Activity {

  public static final String EXTRA_URL = "extra_url";
  public static final String EXTRA_TITLE = "extra_title";

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

    CookieManager cookies = CookieManager.getInstance();
    cookies.setAcceptCookie(true);
    cookies.setAcceptThirdPartyCookies(webView, true);

    // 保留 Cookie 与 DOM storage（含 localStorage）到磁盘：App 冷启动后登录态还在
    CookieManager.getInstance().flush();

    // 不跳系统浏览器：所有链接都留在 App 里（用户要的就是"App 内打开"）
    webView.setWebViewClient(new WebViewClient());
    webView.setWebChromeClient(new WebChromeClient());

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

  /** 顶栏那行小字：只显示 host:port，太长时中间省略（URL 的其它部分对用户没意义） */
  private static String hostOf(String url) {
    if (url == null) {
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
