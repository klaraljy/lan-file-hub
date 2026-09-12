# 后端：core 静态库 + 服务可执行文件 + 迁移工具
# 分层见 docs/architecture.md：api → application → domain → infrastructure/storage

add_library(lan_file_hub_core STATIC
  # common
  src/common/byte_rate_limiter.cpp
  src/common/config.cpp
  src/common/http_util.cpp
  src/common/logging.cpp
  src/common/metrics.cpp
  src/common/net.cpp
  src/common/pagination.cpp
  src/common/rate_limiter.cpp
  src/common/request_id.cpp
  src/common/sha256.cpp
  src/common/uuid.cpp
  # domain
  src/domain/entry_name.cpp
  src/domain/file.cpp
  src/domain/upload_session.cpp
  src/domain/user.cpp
  src/domain/folder.cpp
  # infrastructure
  src/infrastructure/health_check.cpp
  src/infrastructure/database.cpp
  src/infrastructure/file_repository.cpp
  src/infrastructure/password_hasher.cpp
  src/infrastructure/jwt_service.cpp
  src/infrastructure/upload_session_repository.cpp
  src/infrastructure/user_repository.cpp
  src/infrastructure/folder_repository.cpp
  src/infrastructure/audit_repository.cpp
  src/infrastructure/trash_repository.cpp
  src/infrastructure/permission_repository.cpp
  # storage
  src/storage/object_storage.cpp
  src/storage/local_object_storage.cpp
  # application
  src/application/admin_service.cpp
  src/application/app_context.cpp
  src/application/auth_service.cpp
  src/application/file_service.cpp
  src/application/file_scanner.cpp
  src/application/folder_service.cpp
  src/application/permission_checker.cpp
  src/application/upload_service.cpp
  src/application/trash_service.cpp
  src/application/cleanup_service.cpp
  src/application/sharing_service.cpp
  # api
  src/api/error_response.cpp
  src/api/json_body.cpp
  src/api/download_streamer.cpp
  src/api/health_controller.cpp
  src/api/http_metrics.cpp
  src/api/metrics_controller.cpp
  src/api/admin_controller.cpp
  src/api/auth_controller.cpp
  src/api/auth_filter.cpp
  src/api/file_controller.cpp
  src/api/folder_controller.cpp
  src/api/upload_controller.cpp
  src/api/trash_controller.cpp
  src/api/permission_controller.cpp
  src/api/share_controller.cpp
  src/api/request_id_middleware.cpp
  src/api/security_headers.cpp
)

target_include_directories(lan_file_hub_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lan_file_hub_core
  PUBLIC
    Drogon::Drogon
    spdlog::spdlog
    nlohmann_json::nlohmann_json
  PRIVATE
    lan_file_hub_warnings
)

# ---- jwt-cpp（header-only，带 CMake config）----
find_package(jwt-cpp CONFIG REQUIRED)
target_link_libraries(lan_file_hub_core PRIVATE jwt-cpp::jwt-cpp)

# ---- Argon2（vcpkg 的 argon2 port 不提供 CMake config，只能手工查找）----
find_path(ARGON2_INCLUDE_DIR NAMES argon2.h)
find_library(ARGON2_LIBRARY NAMES argon2)
if(ARGON2_INCLUDE_DIR AND ARGON2_LIBRARY)
  target_include_directories(lan_file_hub_core PRIVATE ${ARGON2_INCLUDE_DIR})
  target_link_libraries(lan_file_hub_core PRIVATE ${ARGON2_LIBRARY})
  message(STATUS "argon2: 已启用 Argon2id 密码哈希")
else()
  message(FATAL_ERROR "找不到 argon2（头文件 ${ARGON2_INCLUDE_DIR} / 库 ${ARGON2_LIBRARY}）")
endif()

# ---- 端口自检用的 Winsock（common/net.cpp）----
# Drogon 也会链 ws2_32，这里显式声明一次，避免依赖传递链变化后链接突然失败。
if(WIN32)
  target_link_libraries(lan_file_hub_core PRIVATE ws2_32)
endif()

# ---- 数据库与缓存客户端 ----
# 装不上时给出明确提示，并保留「只编译不探测」的替代路径（见 README「已知限制」）。
find_package(libpqxx CONFIG QUIET)
if(libpqxx_FOUND)
  target_link_libraries(lan_file_hub_core PRIVATE libpqxx::pqxx)
  target_compile_definitions(lan_file_hub_core PRIVATE LAN_FILE_HUB_HAVE_LIBPQXX=1)
  message(STATUS "libpqxx: 已启用 PostgreSQL 支持")
else()
  message(FATAL_ERROR "找不到 libpqxx —— 用户认证依赖数据库，无法降级")
endif()

find_package(hiredis CONFIG QUIET)
if(hiredis_FOUND)
  target_link_libraries(lan_file_hub_core PRIVATE hiredis::hiredis)
  target_compile_definitions(lan_file_hub_core PRIVATE LAN_FILE_HUB_HAVE_HIREDIS=1)
  message(STATUS "hiredis: 已启用 Redis 健康检查")
else()
  message(WARNING "hiredis 未找到 —— Redis 健康检查将降级为“未配置”，构建仍可继续")
endif()

# ---- 服务可执行文件 ----
add_executable(lan_file_hub_server src/main.cpp)
target_link_libraries(lan_file_hub_server PRIVATE lan_file_hub_core lan_file_hub_warnings)

# Drogon 的控制器与过滤器通过静态初始化自注册。静态库里的目标文件如果没人引用，
# 链接器会直接丢掉，控制器就不会注册（表现为所有路由 404）。
# 因此必须整档链接核心库。
if(MSVC)
  target_link_options(lan_file_hub_server PRIVATE "/WHOLEARCHIVE:lan_file_hub_core")
else()
  target_link_options(lan_file_hub_server PRIVATE
    "-Wl,--whole-archive" "$<TARGET_FILE:lan_file_hub_core>" "-Wl,--no-whole-archive")
endif()

# ---- 数据库迁移工具 ----
add_executable(lan_file_hub_migrate src/tools/migrate_main.cpp)
target_link_libraries(lan_file_hub_migrate PRIVATE lan_file_hub_core lan_file_hub_warnings)
if(MSVC)
  target_link_options(lan_file_hub_migrate PRIVATE "/WHOLEARCHIVE:lan_file_hub_core")
endif()

# ---- 微基准（阶段十四）----
# 需要 Google Benchmark；找不到时给出明确提示并跳过，不影响正常构建与测试。
find_package(benchmark CONFIG QUIET)
if(benchmark_FOUND AND NOT LAN_FILE_HUB_SANITIZER)
  add_executable(lan_file_hub_benchmarks benchmarks/core_bench.cpp)
  target_include_directories(lan_file_hub_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
  )
  target_link_libraries(lan_file_hub_benchmarks
    PRIVATE lan_file_hub_core benchmark::benchmark lan_file_hub_warnings_relaxed)
  message(STATUS "benchmark: 已启用微基准目标 lan_file_hub_benchmarks")
else()
  message(STATUS "跳过微基准目标（未安装 Google Benchmark，或当前是 sanitizer 构建：预编译库没有插桩，混链会失败）")
endif()