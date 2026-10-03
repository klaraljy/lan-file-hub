// 「配套手机应用」（安卓安装包）的下载入口。
//   GET /api/v1/app/release   安装包信息：版本 / 大小 / 构建时间 / SHA-256 / 下载地址
//   GET /api/v1/app/download  下载安装包本身（application/octet-stream + Content-Disposition）
//
// 用户 2026-10-03 要求：网页里要有一个**永远指向最新包**的入口，而且"改完就更新的"——
// 重新构建 APK 之后刷新网页，卡片上的大小/时间就是新的，不需要重启后端。
// 实现上：信息与文件都来自 `AppRelease`，每个请求现看磁盘（见 app_release.h 的说明）。
//
// ⛔ 这两个端点**不要登录**，是有意的，不是漏加 AuthFilter：
//   1) 手机上装 App 这一步不该先登录：用户可能用系统浏览器/文件管理器直接打开下载链接；
//   2) APK 是客户端程序本身，里面没有任何用户数据、没有密钥（前端静态资源本来就是公开的，
//      同一个包里就是那些页面）；
//   3) 加鉴权会让 `<a href>` 这种"点一下就能装"的原生下载路径拿不到 Authorization 头，
//      只能绕下载票据——票据是给**用户文件**用的（AGENTS §15.2），不该混进来。
//   风险面：同一个局域网里的人能下到这个 APK，等于能拿到公开的前端代码，可接受；
//   真正的用户数据仍然全靠 AuthFilter + 服务端权限校验。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class AppReleaseController : public drogon::HttpController<AppReleaseController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(AppReleaseController::Describe, "/api/v1/app/release", drogon::Get);
  ADD_METHOD_TO(AppReleaseController::Download, "/api/v1/app/download", drogon::Get);
  METHOD_LIST_END

  static void Describe(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  static void Download(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
