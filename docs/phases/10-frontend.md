# 阶段十：前端完整文件管理界面

> 阶段任务书。阶段编号以 `docs/phases/README.md` 为准；规则、命令与红线见项目根 `AGENTS.md`。

阅读 AGENTS.md，执行项目阶段十。

完善 React + TypeScript 前端，使普通用户可以通过界面完成主要操作。

实现页面：

1. 登录页。
2. 注册页。
3. 我的文件页。
4. 文件夹树或面包屑导航。
5. 文件列表和网格视图。
6. 文件上传面板。
7. 上传任务详情。
8. 文件详情页。
9. 文件版本页。
10. 分享管理页。
11. 回收站页。
12. 个人设置页。
13. 管理员入口。

实现交互：

1. 创建文件夹。
2. 重命名文件。
3. 重命名文件夹。
4. 移动文件和文件夹。
5. 删除文件。
6. 恢复文件。
7. 永久删除文件。
8. 文件下载。
9. 历史版本下载。
10. 历史版本恢复。
11. 分块上传。
12. 断点续传。
13. 暂停上传。
14. 取消上传。
15. 失败重试。
16. 多文件并发上传。
17. 同名文件冲突提示。
18. 权限不足提示。
19. 网络断开提示。
20. 页面刷新后恢复上传任务。

前端状态必须区分：

idle
loading
success
error
retrying
offline
cancelled

上传状态必须区分：

queued
hashing
initiated
uploading
paused
retrying
merging
verifying
completed
failed
cancelled
expired

约束：
- 使用 TanStack Query 管理服务端数据。
- 使用 IndexedDB 保存未完成上传任务的必要信息。
- 不在 LocalStorage 保存密码。
- 所有 API 错误必须展示用户可理解的消息。
- 所有破坏性操作需要确认。
- 所有按钮和菜单必须根据权限显示，但后端仍然必须校验权限。
- 添加前端单元测试和 Playwright 核心流程测试。
- 更新 README，添加页面截图或录屏说明。
