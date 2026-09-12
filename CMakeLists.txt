cmake_minimum_required(VERSION 3.25)

project(lan_file_hub
        VERSION 0.1.0
        DESCRIPTION "LAN file hub: private file management and sharing service"
        LANGUAGES CXX)

# ---- 语言标准（AGENTS.md §2：C++20）----------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---- 输出目录集中，避免产物散落在源码树 -------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---- 选项 -------------------------------------------------------------------
option(LAN_FILE_HUB_BUILD_TESTS "Build unit and integration tests" ON)
option(LAN_FILE_HUB_WERROR "Treat warnings in project code as errors" ON)
set(LAN_FILE_HUB_SANITIZER "" CACHE STRING
    "Sanitizer to enable: address | undefined | thread | address,undefined (empty = off)")

# ---- 统一编译警告 -----------------------------------------------------------
# 警告等级对全项目一致；是否「警告即错误」由 LAN_FILE_HUB_WERROR 控制。
set(LAN_FILE_HUB_WARNING_FLAGS "")
if(MSVC)
  list(APPEND LAN_FILE_HUB_WARNING_FLAGS
    /W4                 # 高警告等级
    /permissive-        # 严格一致性模式
    /utf-8              # 源码与执行字符集均为 UTF-8（源码含中文注释）
    /Zc:__cplusplus     # 让 __cplusplus 反映真实的 C++ 标准
    /EHsc               # 标准 C++ 异常模型
    /external:anglebrackets /external:W0)   # 第三方头文件的警告不打扰我们
else()
  list(APPEND LAN_FILE_HUB_WARNING_FLAGS
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    -Wcast-align -Wunused -Woverloaded-virtual -Wnull-dereference)
endif()

# 严格档：业务代码使用，警告即错误
add_library(lan_file_hub_warnings INTERFACE)
target_compile_options(lan_file_hub_warnings INTERFACE ${LAN_FILE_HUB_WARNING_FLAGS})
if(LAN_FILE_HUB_WERROR)
  if(MSVC)
    target_compile_options(lan_file_hub_warnings INTERFACE /WX)
  else()
    target_compile_options(lan_file_hub_warnings INTERFACE -Werror)
  endif()
endif()

# 宽松档：测试代码使用。GTest 宏会触发常量条件等警告，不适合升级为错误。
add_library(lan_file_hub_warnings_relaxed INTERFACE)
target_compile_options(lan_file_hub_warnings_relaxed INTERFACE ${LAN_FILE_HUB_WARNING_FLAGS})

# ---- Sanitizer ---------------------------------------------------------------
# MSVC 只支持 AddressSanitizer；UBSan / TSan 需要 clang 或 gcc（见 docs/testing.md）
if(LAN_FILE_HUB_SANITIZER)
  if(MSVC)
    if(NOT LAN_FILE_HUB_SANITIZER STREQUAL "address")
      message(FATAL_ERROR
        "MSVC 只支持 address sanitizer，当前请求：${LAN_FILE_HUB_SANITIZER}。"
        " UBSan / TSan 请使用 clang 或 gcc 工具链。")
    endif()
    set(LAN_FILE_HUB_SANITIZER_COMPILE /fsanitize=address)
    set(LAN_FILE_HUB_SANITIZER_LINK /fsanitize=address)
    message(STATUS "Sanitizer: AddressSanitizer (MSVC)")
  else()
    set(LAN_FILE_HUB_SANITIZER_COMPILE -fsanitize=${LAN_FILE_HUB_SANITIZER} -fno-omit-frame-pointer -g)
    set(LAN_FILE_HUB_SANITIZER_LINK -fsanitize=${LAN_FILE_HUB_SANITIZER})
    message(STATUS "Sanitizer: ${LAN_FILE_HUB_SANITIZER}")
  endif()
  # 两个警告接口都要挂上：core 用严格接口，测试与工具用宽松接口，
  # 少挂一个就会出现 "annotate_string 不匹配" 这类链接错误（ASan 与非 ASan 目标混链）。
  target_compile_options(lan_file_hub_warnings INTERFACE ${LAN_FILE_HUB_SANITIZER_COMPILE})
  target_link_options(lan_file_hub_warnings INTERFACE ${LAN_FILE_HUB_SANITIZER_LINK})
  target_compile_options(lan_file_hub_warnings_relaxed INTERFACE ${LAN_FILE_HUB_SANITIZER_COMPILE})
  target_link_options(lan_file_hub_warnings_relaxed INTERFACE ${LAN_FILE_HUB_SANITIZER_LINK})
endif()

# ---- 依赖（vcpkg manifest 模式，见 vcpkg.json）-------------------------------
find_package(Drogon CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(backend)

if(LAN_FILE_HUB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(backend/tests)
endif()

message(STATUS "lan-file-hub ${PROJECT_VERSION} | build=${CMAKE_BUILD_TYPE} "
               "| tests=${LAN_FILE_HUB_BUILD_TESTS} | werror=${LAN_FILE_HUB_WERROR}")
