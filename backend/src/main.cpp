// 服务入口。
// 启动顺序：配置 → 日志 → 存储目录 → 数据库连接池 → 服务装配 → HTTP 服务。
// 任何一步失败都必须明确报错并非零退出，绝不带病启动（docs/deployment.md）。
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
#include "lan_file_hub/application/admin_service.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/logging.h"
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

  // 3) 存储根目录
  std::error_code ec;
  std::filesystem::create_directories(config.storage.root, ec);
  if (ec) {
    const std::string message = "无法创建存储根目录: " + config.storage.root.string();
    spdlog::error("{}（{}）", message, ec.message());
    return Fail(message);
  }

  // 4) 数据库连接池（用户认证起就需要数据库，连不上直接失败）
  auto pool = ConnectionPool::Create(config.database, 4);
  if (!pool) {
    const std::string detail = Redact(pool.GetError().detail, secrets);
    spdlog::error("数据库连接失败 | {} | {}", pool.GetError().message, detail);
    return Fail(pool.GetError().message + ": " + detail);
  }
  spdlog::info("数据库连接池就绪 | 连接数={}", pool.Value()->size());

  // 5) 对象存储
  auto storage = LocalObjectStorage::Create(config.storage.root);
  if (!storage) {
    spdlog::error("对象存储初始化失败 | {}", storage.GetError().message);
    return Fail(storage.GetError().message);
  }

  // 6) 服务装配
  auto context = std::make_shared<AppContext>();
  context->pool = pool.Value();
  context->secrets = secrets;
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
  context->folders_service =
      std::make_shared<FolderService>(*context->folders, *context->permissions);
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
      AuthServiceOptions{config.max_user_storage_bytes}, context->folders.get());
  context->trash_service = std::make_shared<TrashService>(
      *context->trash, *context->files, *context->folders, *context->audit, *context->cleanup_tasks,
      *context->permissions_repo, *context->shares, *context->permissions);
  context->sharing_service = std::make_shared<SharingService>(
      *context->permissions_repo, *context->shares, *context->files, *context->folders,
      *context->users, *context->audit, *context->storage, *context->permissions);
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
  // 下载限速（阶段十二）：0 表示不限速；开启后下载改走异步 chunked 流按令牌桶发送
  context->download_limiter =
      std::make_shared<ByteRateLimiter>(config.download_rate_limit_bytes_per_second);
  if (context->download_limiter->enabled()) {
    spdlog::info("下载限速已启用 | bytes_per_second={}",
                 context->download_limiter->bytes_per_second());
  }
  SetAppContext(context);

  if (!config.IsProduction() && config.jwt_secret.empty()) {
    spdlog::warn("JWT_SECRET 未设置（开发环境可登录但 Token 无法签发；生产环境会拒绝启动）");
  }

  // 7) 健康检查器（PostgreSQL / Redis / 对象存储）
  SetHealthChecker(
      std::make_shared<HealthChecker>(config.database, config.redis, config.storage, secrets));

  // 8) HTTP 服务：request_id 中间件
  // 注意：AuthFilter 继承自 drogon::HttpFilter<AuthFilter>，由 Drogon 自动注册
  //（手动 registerFilter 会触发静态断言），控制器里按名字 "AuthFilter" 引用即可。
  RegisterRequestIdMiddleware();
  // CORS 白名单 + 安全响应头（阶段十二第 11、16 条）。白名单来自配置，禁止 "*"
  RegisterSecurityAdvices(config.cors_allowed_origins);
  // HTTP 指标（阶段十三）：请求数、状态码分布、响应延迟
  RegisterHttpMetrics();

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
