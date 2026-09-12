# 阶段十三：可观测性和管理员功能

> 阶段任务书。阶段编号以 `docs/phases/README.md` 为准；规则、命令与红线见项目根 `AGENTS.md`。

阅读 AGENTS.md，执行项目阶段十三。

实现可观测性和管理员功能：

1. 完善结构化日志。
2. 每个请求生成 request_id。
3. 支持传递客户端 X-Request-Id，但必须校验长度和格式。
4. 记录请求耗时。
5. 记录上传成功、失败和重试次数。
6. 记录下载次数和下载失败原因。
7. 添加 Prometheus 指标。
8. 添加健康检查指标。
9. 添加当前上传数指标。
10. 添加磁盘使用率指标。
11. 添加 API 错误数量指标。
12. 添加响应延迟指标。
13. 创建管理员用户列表。
14. 实现管理员启用和禁用用户。
15. 实现管理员修改用户配额。
16. 实现系统存储统计。
17. 实现审计日志查询。
18. 添加管理员接口权限测试。
19. 添加前端管理员页面。
20. 更新 README 和部署文档。

建议指标：

lan_file_hub_http_requests_total
lan_file_hub_http_request_duration_seconds
lan_file_hub_uploads_total
lan_file_hub_upload_bytes_total
lan_file_hub_active_uploads
lan_file_hub_storage_used_bytes
lan_file_hub_storage_free_bytes
lan_file_hub_background_jobs_total
lan_file_hub_errors_total

约束：
- 管理员页面不能展示密码、密码哈希、Token 或敏感配置。
- 审计日志不能被普通用户读取。
- 管理员对文件的特殊访问必须留下审计记录。
- 指标标签不能直接使用无限制的用户输入，防止高基数问题。
