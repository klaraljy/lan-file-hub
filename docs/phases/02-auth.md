# 阶段二：用户认证

> 阶段任务书。阶段编号以 `docs/phases/README.md` 为准；规则、命令与红线见项目根 `AGENTS.md`。

阅读 AGENTS.md，执行项目阶段二。

实现用户认证模块：

1. 创建 users 数据库迁移。
2. 实现用户注册。
3. 实现用户登录。
4. 使用 Argon2id 或 bcrypt 进行密码哈希。
5. 禁止明文保存密码。
6. 实现 JWT 或安全 Session 认证。
7. 实现认证中间件。
8. 实现 GET /api/v1/auth/me。
9. 支持 ADMIN 和 USER 角色。
10. 实现用户 ACTIVE、DISABLED 状态。
11. 禁止被禁用用户登录和访问受保护接口。
12. 实现统一错误码。
13. 添加注册、登录、错误密码、重复用户、禁用用户测试。
14. 添加认证中间件测试。
15. 添加前端登录页。
16. 添加前端注册页。
17. 添加前端登录态恢复。
18. 更新 API 文档和 README。

必须实现的接口：

POST /api/v1/auth/register
POST /api/v1/auth/login
POST /api/v1/auth/logout
GET  /api/v1/auth/me

安全要求：
- 不在日志中输出密码、Token 或密码哈希。
- 使用参数化数据库查询。
- 登录失败返回通用错误，避免泄露用户是否存在。
- Token 过期后返回 TOKEN_EXPIRED。
- 不能用前端隐藏按钮代替后端权限检查。
- JWT_SECRET 不得写入源码。
- 前端不得将密码保存到 LocalStorage。
- 最终报告必须说明 Token 的保存和失效策略。
