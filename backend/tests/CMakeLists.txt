# 单元测试 + 集成测试。
# 集成测试需要真实 PostgreSQL；连不上时会自动跳过（见测试文件里的说明），
# 设置 LAN_FILE_HUB_REQUIRE_DB=1 可让缺失的数据库变成失败。
find_package(GTest CONFIG REQUIRED)

add_executable(lan_file_hub_unit_tests
  unit/config_test.cpp
  unit/request_id_test.cpp
  unit/uuid_test.cpp
  unit/health_check_test.cpp
  unit/password_hasher_test.cpp
  unit/jwt_service_test.cpp
  unit/auth_validation_test.cpp
  unit/folder_validation_test.cpp
  unit/pagination_test.cpp
  unit/sha256_test.cpp
  unit/http_util_test.cpp
  unit/object_storage_test.cpp
  unit/upload_session_test.cpp
  unit/rate_limiter_test.cpp
  unit/json_body_test.cpp
  unit/byte_rate_limiter_test.cpp
  unit/security_headers_test.cpp
  unit/file_scanner_test.cpp
  unit/logging_redaction_test.cpp
  unit/metrics_test.cpp
  unit/net_test.cpp
  unit/presence_registry_test.cpp
  unit/invite_test.cpp
  unit/app_release_test.cpp
)

target_include_directories(lan_file_hub_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(lan_file_hub_unit_tests
  PRIVATE
    lan_file_hub_core
    GTest::gtest_main
    lan_file_hub_warnings_relaxed
)

add_executable(lan_file_hub_integration_tests
  integration/auth_flow_test.cpp
  integration/folder_flow_test.cpp
  integration/file_flow_test.cpp
  integration/upload_flow_test.cpp
  integration/dedup_quota_test.cpp
  integration/version_conflict_test.cpp
  integration/trash_flow_test.cpp
  integration/sharing_flow_test.cpp
  integration/admin_flow_test.cpp
  integration/user_directory_test.cpp
  integration/invite_flow_test.cpp
  integration/recent_files_test.cpp
  integration/fault_injection_test.cpp
)

target_include_directories(lan_file_hub_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(lan_file_hub_integration_tests
  PRIVATE
    lan_file_hub_core
    GTest::gtest_main
    lan_file_hub_warnings_relaxed
)

include(GoogleTest)
# WORKING_DIRECTORY 必须是项目根：集成测试夹具用相对路径读 .env（数据库/Redis/存储配置），
# 而 `ctest --test-dir build/debug` 会把工作目录设成 build/debug，导致读不到 .env、
# 所有打库的用例直接失败（CI 上真的踩到过）。
gtest_discover_tests(lan_file_hub_unit_tests
  PROPERTIES LABELS "unit"
  DISCOVERY_TIMEOUT 30
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
gtest_discover_tests(lan_file_hub_integration_tests
  PROPERTIES LABELS "integration"
  DISCOVERY_TIMEOUT 30
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
