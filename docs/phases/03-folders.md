# 阶段三：文件夹和基础资源模型

> 阶段任务书。阶段编号以 `docs/phases/README.md` 为准；规则、命令与红线见项目根 `AGENTS.md`。

阅读 AGENTS.md，执行项目阶段三。

实现文件夹和基础资源模块：

1. 创建 folders 数据库迁移。
2. 创建用户根目录。
3. 实现文件夹创建。
4. 实现文件夹列表。
5. 实现文件夹详情。
6. 实现文件夹重命名。
7. 实现文件夹移动。
8. 实现文件夹软删除。
9. 实现 GET /api/v1/folders/{folder_id}/children。
10. 实现 POST /api/v1/folders。
11. 实现 PATCH /api/v1/folders/{folder_id}。
12. 实现 DELETE /api/v1/folders/{folder_id}。
13. 实现文件夹名称校验。
14. 实现同一父目录下的名称冲突检测。
15. 实现根目录保护。
16. 实现路径穿越防护。
17. 实现基础资源权限检查接口。
18. 添加文件夹 Repository、Application Service 和 Controller。
19. 添加单元测试和数据库集成测试。
20. 添加前端文件夹树或面包屑导航。
21. 添加新建、重命名和删除文件夹交互。
22. 更新 docs/api.md 和 README。

约束：
- 不在 Controller 中直接执行 SQL。
- 不在 Controller 中直接操作文件系统。
- 文件夹名称不能包含 /、\\、控制字符、. 或 ..。
- 不允许移动文件夹到自身或其子目录。
- 软删除资源不能出现在普通目录列表中。
- 删除和移动操作必须记录操作者。
