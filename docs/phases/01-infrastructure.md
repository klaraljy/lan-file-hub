# 阶段一：工程基础设施

> 阶段任务书。阶段编号以 `docs/phases/README.md` 为准；规则、命令与红线见项目根 `AGENTS.md`。

阅读 AGENTS.md，执行项目阶段一。

实现内容：
1. 初始化 C++20 CMake 工程。
2. 建立以下目录：
   - backend/include
   - backend/src
   - backend/tests
   - migrations
   - docs
   - scripts
   - frontend
3. 配置 Debug 和 Release 构建。
4. 配置 clang-format。
5. 配置 clang-tidy。
6. 配置统一编译警告。
7. 配置 AddressSanitizer 和 UndefinedBehaviorSanitizer。
8. 实现配置读取模块。
9. 实现环境变量和 .env.example 支持。
10. 实现 spdlog 日志模块。
11. 为每个请求预留 request_id。
12. 实现 GET /health/live。
13. 实现 GET /health/ready。
14. 实现 PostgreSQL 健康检查。
15. 实现 Redis 健康检查。
16. 添加 Dockerfile。
17. 添加 docker-compose.yml，包含：
    - backend
    - frontend
    - postgres
    - redis
18. 添加最小 GoogleTest 测试。
19. 添加 GitHub Actions 编译和测试流程。
20. 更新 README，写明本地启动步骤。

约束：
- 不实现用户、文件、上传等业务功能。
- 不引入不必要的第三方依赖。
- 健康检查必须区分存活和就绪：
  - live 只表示进程正在运行。
  - ready 表示必要依赖可用。
- 必须能够本地编译。
- 必须运行测试。
- 如果某个依赖无法安装，说明原因并保留可编译的替代方案。
- 最终报告必须列出修改文件、构建命令、测试命令和结果。
