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
    titleView.setText(title == null ? getString(R.string.app_name) : title);

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

    // 不跳系统浏览器：所有链接都留在 App 里（用户要的就是"App 内打开"）
    webView.setWebViewClient(new WebViewClient());
    webView.setWebChromeClient(new WebChromeClient());

    if (url != null) {
      webView.loadUrl(url);
    } else {
      finish();
      return;
    }

    findViewById(R.id.web_reload).setOnClickListener(view -> webView.reload());
    findViewById(R.id.web_external).setOnClickListener(
        view -> {
          Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
          startActivity(intent);
        });
    findViewById(R.id.web_close).setOnClickListener(view -> finish());
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
