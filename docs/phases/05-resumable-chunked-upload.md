# 阶段五：可恢复分块上传

> 阶段任务书。阶段编号以 `docs/phases/README.md` 为准；规则、命令与红线见项目根 `AGENTS.md`。

阅读 AGENTS.md，执行项目阶段五。

实现可恢复的分块上传：

1. 创建 upload_sessions 表。
2. 创建 upload_chunks 表。
3. 实现创建上传会话 API。
4. 实现上传分块 API。
5. 实现查询上传状态 API。
6. 实现完成上传 API。
7. 实现取消上传 API。
8. 支持重复分块幂等。
9. 支持状态查询。
10. 支持失败重试。
11. 支持断点续传。
12. 支持客户端提交文件大小。
13. 支持客户端提交 SHA-256。
14. 完成上传时由服务端重新计算文件大小和 SHA-256。
15. 实现分块哈希校验。
16. 实现上传会话过期时间。
17. 实现上传会话所有者校验。
18. 防止多个请求同时完成同一个上传会话。
19. 实现用户级和全局并发上传限制。
20. 添加 PostgreSQL 集成测试。
21. 添加重复分块测试。
22. 添加缺失分块测试。
23. 添加中断后恢复测试。
24. 添加哈希不匹配测试。
25. 添加并发完成测试。
26. 更新 docs/api.md、docs/recovery.md 和 README。
27. 更新前端分块上传组件。

必须实现的接口：

POST   /api/v1/uploads
GET    /api/v1/uploads/{upload_id}
PUT    /api/v1/uploads/{upload_id}/chunks/{chunk_index}
POST   /api/v1/uploads/{upload_id}/complete
DELETE /api/v1/uploads/{upload_id}

约束：
- 默认分块大小为 8 MiB。
- 允许的分块大小必须有上下限。
- 不得一次性读取整个文件。
- 分块索引从 0 开始。
- 同一个分块重复上传且内容一致时返回成功。
- 同一个分块重复上传但内容不同，返回 CHUNK_CONFLICT。
- 不允许跳过权限校验。
- 上传会话必须持久化到 PostgreSQL。
- Redis 不能作为上传会话的唯一来源。
- 完成上传必须是幂等的。
- 服务端必须重新计算最终 SHA-256。
- 前端刷新页面后必须能够恢复上传状态。
