# 阶段九：资源权限和共享链接

> 阶段任务书。阶段编号以 `docs/phases/README.md` 为准；规则、命令与红线见项目根 `AGENTS.md`。

阅读 AGENTS.md，执行项目阶段九。

实现资源权限和分享链接：

1. 创建 permissions 表。
2. 实现 READ、WRITE、OWNER 权限。
3. 实现用户级资源授权。
4. 实现权限查询。
5. 实现权限修改。
6. 实现权限删除。
7. 实现文件夹权限继承。
8. 实现资源访问时的后端权限检查。
9. 创建分享链接。
10. 分享链接支持只读权限。
11. 支持分享链接过期时间。
12. 支持撤销分享链接。
13. 支持最大访问次数或下载次数。
14. 服务端只保存分享 Token 哈希。
15. 实现公开分享查看接口。
16. 实现公开分享下载接口。
17. 防止分享链接越权访问其他资源。
18. 添加权限矩阵测试。
19. 添加分享过期测试。
20. 添加分享撤销测试。
21. 添加分享下载次数测试。
22. 添加前端权限管理页面。
23. 添加前端分享链接页面。
24. 更新 docs/permissions.md、docs/api.md 和 README。

必须实现的接口：

GET    /api/v1/resources/{resource_id}/permissions
PUT    /api/v1/resources/{resource_id}/permissions/{user_id}
DELETE /api/v1/resources/{resource_id}/permissions/{user_id}

POST   /api/v1/resources/{resource_id}/shares
GET    /api/v1/resources/{resource_id}/shares
DELETE /api/v1/shares/{share_id}

GET    /api/v1/public/shares/{token}
GET    /api/v1/public/shares/{token}/download

约束：
- 前端隐藏按钮不能替代后端权限检查。
- 非所有者不能随意修改权限。
- 分享 Token 必须使用高强度随机值。
- 分享 Token 明文只在创建时返回。
- 管理员访问用户资源时仍然需要审计日志。
- 权限继承规则必须写入文档。
