// 服务入口。
// 启动顺序：配置 → 日志 → 存储目录 → 数据库连接池 → 服务装配 → HTTP 服务。
// 任何一步失败都必须明确报错并非零退出，绝不带病启动（docs/deployment.md）。
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include "api/auth_filter.h"
#include "api/http_metrics.h"
#include "api/request_id_middleware.h"
#include "lan_file_hub/api/security_headers.h"
#include "lan_file_hub/api/web_hosting.h"
#include "lan_file_hub/application/admin_service.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/logging.h"
#include "lan_file_hub/common/net.h"
#include "lan_file_hub/domain/upload_session.h"
#include "lan_file_hub/infrastructure/health_check.h"
#include "lan_file_hub/storage/local_object_storage.h"

namespace {

int Fail(const std::string& message) {
  std::cerr << "[lan-file-hub] 启动失败: " << message << '\n';
  return 1;
}

// 把任意文本里的凭据替换掉，避免连接串进日志
std::string Redact(const std::string& text, const std::vector<std::string>& secrets) {
  return lan_file_hub::RedactSecrets(text, secrets);
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
  using namespace lan_file_hub;

#if defined(_WIN32)
  // 服务进程一律不能弹崩溃/断言对话框：没人点击时进程会永远挂在那里——既不监听端口、
  // 也不退出，还锁住 exe 让重新构建失败（实测：端口被占导致 Drogon 断言，留下这种僵尸进程）。
  ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
#if defined(_MSC_VER)
  // abort 时不要写错误消息、不要走 Windows 错误报告，直接终止
  ::_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  // 1) 配置：.env（工作目录）+ 进程环境变量，环境变量优先
  auto loaded = LoadConfig(".env");
  if (!loaded.Ok()) {
    std::string message = loaded.GetError().message;
    if (!loaded.GetError().detail.empty()) {
      message += "（" + loaded.GetError().detail + "）";
    }
    return Fail(message);
  }
  const AppConfig& config = loaded.Value().config;
  const std::vector<std::string> secrets = loaded.Value().secrets;

  // 2) 日志
  auto logging = InitializeLogging(LoggingOptions{config.log_level, config.log_file, secrets});
  if (!logging.Ok()) {
    return Fail(logging.GetError().message + ": " + logging.GetError().detail);
  }

  spdlog::info("lan-file-hub 启动中 | env={} | 监听端口={} | 线程数={}", config.app_env,
               config.http_port, config.http_threads);

  // 3) 监听端口自检（最便宜的失败先报）
  // Drogon 绑定失败时只打一行 FATAL 日志，进程仍留在事件循环里不退出——外部看到的是
  // 「进程活着、端口没人听」，容器编排会以为服务已起，一键启动脚本也会被误导。
  // 所以这里自己先探一次，不可用就明确报错并非零退出，绝不带病启动。
  const PortStatus port_status = CheckListenPort(static_cast<std::uint16_t>(config.http_port));
  if (port_status != PortStatus::kAvailable && port_status != PortStatus::kUnknown) {
    std::string message = "端口 " + std::to_string(config.http_port);
    if (port_status == PortStatus::kForbidden) {
      // 本机真的出现过：Docker Desktop 预留的端口段（7920-8419）覆盖了 8080，
      // 没有任何进程在监听，bind 却报 WSAEACCES。只提示"被占用"会把排查方向带偏。
      message +=
          " 被系统保留或禁止绑定（Windows 保留端口段，或权限不足）。"
          "用 netsh int ipv4 show excludedportrange protocol=tcp 查看保留段，"
          "再换一个端口（.env 的 HTTP_PORT）或释放该保留段";
    } else {
      message += " 已被占用：可能已有一个实例在运行。先停掉它（或改 .env 的 HTTP_PORT）";
    }
    spdlog::error("{}", message);
    return Fail(message);
  }

  // 4) 存储根目录
  std::error_code ec;
  std::filesystem::create_directories(config.storage.root, ec);
  if (ec) {
    const std::string message = "无法创建存储根目录: " + config.storage.root.string();
    spdlog::error("{}（{}）", message, ec.message());
    return Fail(message);
  }

  // 5) 数据库连接池（用户认证起就需要数据库，连不上直接失败）
  auto pool = ConnectionPool::Create(config.database, 4);
  if (!pool) {
    const std::string detail = Redact(pool.GetError().detail, secrets);
    spdlog::error("数据库连接失败 | {} | {}", pool.GetError().message, detail);
    return Fail(pool.GetError().message + ": " + detail);
  }
  spdlog::info("数据库连接池就绪 | 连接数={}", pool.Value()->size());

  // 6) 对象存储
  auto storage = LocalObjectStorage::Create(config.storage.root);
  if (!storage) {
    spdlog::error("对象存储初始化失败 | {}", storage.GetError().message);
    return Fail(storage.GetError().message);
  }

  // 7) 服务装配
  auto context = std::make_shared<AppContext>();
  context->pool = pool.Value();
  context->secrets = secrets;
  // 管理端「文件存在哪」用的路径（只读展示，不含凭据）
  context->storage_root_path = config.storage.root.string();
  context->storage = storage.Value();
  context->users = std::make_shared<UserRepository>(*context->pool);
  context->folders = std::make_shared<FolderRepository>(*context->pool);
  context->files = std::make_shared<FileRepository>(*context->pool);
  context->uploads = std::make_shared<UploadSessionRepository>(*context->pool);
  context->trash = std::make_shared<TrashRepository>(*context->pool);
  context->audit = std::make_shared<AuditLogRepository>(*context->pool);
  context->cleanup_tasks = std::make_shared<CleanupTaskRepository>(*context->pool);
  context->permissions_repo = std::make_shared<PermissionRepository>(*context->pool);
  context->shares = std::make_shared<ShareLinkRepository>(*context->pool);
  context->invites_repo = std::make_shared<InviteRepository>(*context->pool);
  context->password_hasher = std::make_shared<PasswordHasher>();
  context->jwt = std::make_shared<JwtService>(
      JwtOptions{config.jwt_secret, config.jwt_expire_seconds, "lan-file-hub"});
  // 阶段九：基于 permissions 表 + 文件夹继承的权限检查（READ < WRITE < OWNER）
  context->permissions = std::make_shared<GrantBasedPermissionChecker>(
      *context->permissions_repo, *context->folders, *context->files);
  // 内容安全扫描（阶段十二第 17 条）：本期只有 NoopFileScanner（明确返回 skipped，
  // 不假装做过检测）。接入真实引擎时在这里替换实现即可。
  context->scanner = std::make_shared<NoopFileScanner>();
  spdlog::warn("病毒扫描：未接入真实引擎（{}），上传内容不会被扫描；详见 README「已知限制」",
               context->scanner->name());
  context->folders_service = std::make_shared<FolderService>(
      *context->folders, *context->files, *context->permissions);
  context->files_service = std::make_shared<FileService>(
      *context->files, *context->folders, *context->storage, *context->permissions,
      FileServiceOptions{config.max_file_size_bytes, config.min_free_disk_bytes});
  context->uploads_service = std::make_shared<UploadService>(
      *context->uploads, *context->files, *context->folders, *context->users, *context->storage,
      *context->permissions,
      UploadServiceOptions{static_cast<std::uint32_t>(config.default_chunk_size_bytes),
                           config.max_file_size_bytes, config.upload_session_ttl_seconds,
                           config.max_concurrent_uploads, config.max_concurrent_uploads_per_user,
                           config.min_free_disk_bytes, context->scanner});
  // 管理员服务（阶段十三）：权限判定与审计留痕都在服务层
  context->admin_service = std::make_shared<AdminService>(
      *context->users, *context->files, *context->audit, *context->uploads, *context->storage);
  context->auth = std::make_shared<AuthService>(
      *context->users, *context->password_hasher, *context->jwt,
      AuthServiceOptions{config.max_user_storage_bytes, config.jwt_remember_expire_seconds},
      context->folders.get());
  context->trash_service = std::make_shared<TrashService>(
      *context->trash, *context->files, *context->folders, *context->audit, *context->cleanup_tasks,
      *context->permissions_repo, *context->shares, *context->permissions);
  context->sharing_service = std::make_shared<SharingService>(
      *context->permissions_repo, *context->shares, *context->files, *context->folders,
      *context->users, *context->audit, *context->storage, *context->permissions,
      *context->password_hasher, *context->jwt);
  // 邀请制注册（用户 2026-10-02）：ALLOW_SELF_REGISTRATION=false 时注册必须带有效邀请
  context->invites = std::make_shared<InviteService>(*context->invites_repo);
  context->allow_self_registration = config.allow_self_registration;
  if (!config.allow_self_registration) {
    spdlog::info("自助注册已关闭（ALLOW_SELF_REGISTRATION=false）：注册需要管理员发的邀请链接");
  }
  // 后台清理：过期回收站 + 对象异步删除（阶段八），阶段十一继续扩展
  context->cleanup_service =
      std::make_shared<CleanupService>(*context->cleanup_tasks, *context->files, *context->storage,
                                       *context->trash_service, *context->uploads);
  // 限流器（阶段十二）：窗口统一 1 分钟。桶数量上限防止用随机 key 打内存。
  context->login_limiter = std::make_shared<RateLimiter>(
      static_cast<int>(config.login_rate_limit_per_minute), std::chrono::seconds(60));
  context->login_ip_limiter = std::make_shared<RateLimiter>(
      static_cast<int>(config.login_ip_rate_limit_per_minute), std::chrono::seconds(60));
  context->upload_limiter = std::make_shared<RateLimiter>(
      static_cast<int>(config.upload_rate_limit_per_minute), std::chrono::seconds(60));
  // 分享密码解锁（2026-10-03）：按 IP + Token 限流，防止拿公开接口猜密码
  context->share_unlock_limiter = std::make_shared<RateLimiter>(
      static_cast<int>(config.share_unlock_rate_limit_per_minute), std::chrono::seconds(60));
  // 下载限速（阶段十二）：0 表示不限速；开启后下载改走异步 chunked 流按令牌桶发送
  context->download_limiter =
      std::make_shared<ByteRateLimiter>(config.download_rate_limit_bytes_per_second);
  if (context->download_limiter->enabled()) {
    spdlog::info("下载限速已启用 | bytes_per_second={}",
                 context->download_limiter->bytes_per_second());
  }
  // 在线状态（五阶段改造 ⑤：广场）：进程内存 + 心跳超时，不落库、重启即清空
  context->presence = std::make_shared<PresenceRegistry>();
  // 配套手机应用的安装包（用户 2026-10-03）：只记路径，每次请求现看磁盘，
  // 所以 scripts\build-android.ps1 重新出包之后网页刷新就是新数字，不用重启后端
  context->app_release = std::make_shared<AppRelease>(config.app_apk_path);
  if (config.app_apk_path.empty()) {
    spdlog::info(
        "未配置安卓安装包位置（APP_APK_PATH / WEB_ROOT 都没有）：下载入口会显示"
        "「还没有安装包」");
  } else {
    const auto release = context->app_release->Describe();
    if (release.available) {
      spdlog::info("安卓安装包 | path={} | version={} | size={} 字节 | built_at={}",
                   config.app_apk_path.string(), release.version_name, release.size_bytes,
                   release.built_at);
    } else {
      spdlog::info("安卓安装包尚未构建 | 期望位置={}（跑一次 scripts\\build-android.ps1 就有了）",
                   config.app_apk_path.string());
    }
  }
  SetAppContext(context);
  if (!config.IsProduction() && config.jwt_secret.empty()) {
    spdlog::warn("JWT_SECRET 未设置（开发环境可登录但 Token 无法签发；生产环境会拒绝启动）");
  }

  // 8) 健康检查器（PostgreSQL / Redis / 对象存储）
  SetHealthChecker(
      std::make_shared<HealthChecker>(config.database, config.redis, config.storage, secrets));

  // 9) HTTP 服务：request_id 中间件
  // 注意：AuthFilter 继承自 drogon::HttpFilter<AuthFilter>，由 Drogon 自动注册
  //（手动 registerFilter 会触发静态断言），控制器里按名字 "AuthFilter" 引用即可。
  RegisterRequestIdMiddleware();
  // CORS 白名单 + 安全响应头（阶段十二第 11、16 条）。白名单来自配置，禁止 "*"
  RegisterSecurityAdvices(config.cors_allowed_origins);
  // HTTP 指标（阶段十三）：请求数、状态码分布、响应延迟
  RegisterHttpMetrics();

  // 前端静态托管（五阶段改造 ④）：设置了 WEB_ROOT 就由后端托管前端 dist，
  // 便携包因此只有一个端口、一个进程，不需要 Node.js 或 nginx。
  if (const Status hosting = RegisterWebHosting(config.web_root); !hosting.Ok()) {
    spdlog::error("{}", hosting.GetError().message);
    return Fail(hosting.GetError().message);
  }

  // 后台清理线程随服务启动；进程退出时由 CleanupService 析构有序 join
  if (context->cleanup_service) {
    context->cleanup_service->Start();
  }

  drogon::app().addListener("0.0.0.0", config.http_port);
  drogon::app().setThreadNum(config.http_threads);
  drogon::app().setIdleConnectionTimeout(60);
  // 单个请求体的上限：没有任何端点接收"整个文件"——上传一律走分块（每块 ≤ kMaxChunkSize），
  // 合并发生在服务端。所以上限取"最大分块 + 1 MiB 余量"，Drogon 会在读取请求体之前
  // 就按这个上限拒绝超大请求（413），而不是先缓冲/落盘再交给业务层拒绝（阶段十二验收项）。
  drogon::app().setClientMaxBodySize(static_cast<std::size_t>(kMaxChunkSize) + (1U << 20));
  // 内存里最多缓冲 1 MiB：超过这个大小的请求体 Drogon 会交给 RequestStream 流式处理，
  // 于是整个文件不会进内存（AGENTS.md §5 红线）。
  drogon::app().setClientMaxMemoryBodySize(1U << 20);
  // 不开这个开关，createRequestStream() 会返回空（表现为大文件上传 500）
  drogon::app().enableRequestStream(true);
  drogon::app().registerBeginningAdvice([]() {
    spdlog::info("HTTP 服务已就绪 | 健康检查: /health/live, /health/ready | 接口: /api/v1/*");
  });

  spdlog::info("进入事件循环（Ctrl+C 退出）");
  drogon::app().run();

  // 先停后台线程再关日志，避免退出阶段还在写日志
  if (context->cleanup_service) {
    context->cleanup_service->Stop();
  }
  spdlog::info("服务已退出");
  ShutdownLogging();
  return 0;
}
