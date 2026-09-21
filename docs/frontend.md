# 前端页面、状态与上传算法

技术栈、目录结构与命令见[项目根 AGENTS.md](../AGENTS.md) 第 2、3、4 节。

## 1. 页面清单

普通用户必须能通过界面完成主要操作，页面清单如下：

| # | 页面 | 对应要素 |
|---|---|---|
| 1 | 登录页 | 2.1 |
| 2 | 注册页 | 登录页提供注册入口（2.1） |
| 3 | 我的文件页 | 2.2 |
| 4 | 文件夹树或面包屑导航 | 2.2 |
| 5 | 文件列表和网格视图 | 2.2 |
| 6 | 文件上传面板 | 2.3 |
| 7 | 上传任务详情 | 2.3 |
| 8 | 文件详情页 | 2.4 |
| 9 | 文件版本页 | 2.4（历史版本） |
| 10 | 分享管理页 | [permissions.md](permissions.md) |
| 11 | 回收站页 | 2.6 |
| 12 | 个人设置页 | 2.2（左侧导航「设置」） |
| 13 | 管理员入口 | 2.7 |

## 2. 各页面要素

### 2.1 登录页

功能：

- 用户名或邮箱输入。
- 密码输入。
- 登录按钮。
- 注册入口。
- 错误提示。
- 加载状态。
- 登录失败后保留非敏感表单内容。

### 2.2 文件主页

页面包含左侧导航：

- 我的文件
- 最近文件
- 分享给我的
- 回收站
- 设置

页面顶部与操作区：

- 顶部面包屑。
- 新建文件夹按钮。
- 上传按钮。
- 搜索框。
- 文件列表或网格切换。
- 排序菜单。
- 多选操作。
- 文件右键菜单或操作菜单。

文件列表字段：

```text
名称
类型
大小
修改时间
所有者
权限
操作
```

### 2.3 上传交互

上传组件必须支持：

- 选择多个文件。
- 拖拽上传。
- 显示单文件进度。
- 显示总体进度。
- 暂停或取消上传。
- 网络中断提示。
- 自动重试有限次数。
- 页面刷新后恢复未完成上传。
- 哈希计算状态。
- 同名文件处理策略。
- 上传失败后手动重试。
- 上传完成后刷新目录。

前端不能假设分块一定按顺序到达。

### 2.4 文件详情

显示：

- 文件名。
- 类型。
- 大小。
- SHA-256。
- 当前版本。
- 创建者。
- 修改时间。
- 权限。
- 分享链接。
- 历史版本。
- 操作记录。

### 2.5 分享弹窗

分享弹窗的字段、交互与明文 Token 展示规则见 [permissions.md](permissions.md)。

链接地址由 `src/services/publicUrl.ts` 统一生成：**用户用本机地址（127.0.0.1/localhost）打开页面时，
自动换成局域网地址并给出提示** —— 否则生成的链接在别人手机上指向手机自己，必然打不开
（AGENTS.md §14.4，真实故障）。局域网地址由 `scripts/start.ps1` 通过 `LFH_LAN_URL`
→ vite `define` → `import.meta.env.VITE_LFH_LAN_URL` 注入。

### 2.5.1 发送给…（按用户分享，2026-09-21 新增）

解决"我要把这个文件发给某个人"这件事 —— 面向不懂技术的用户，全程不出现 UUID、不出现"权限位"这类词。

- 入口：**主按钮「发送给…」**（`type="primary"`），三个地方都有，都在最显眼的位置：
  列表行（文件 / 文件夹）、网格卡片、文件详情抽屉。
- 弹窗（`src/components/SendToDialog.tsx`）分两块：
  1. **选人 → 选对方能做什么 → 发送**：选人走 `GET /api/v1/users/lookup?q=`（`src/services/usersApi.ts`
     + `src/features/sharing/useUserLookup.ts`，关键词 ≥2 字、300ms 防抖），下拉里只显示用户名；
     权限用大白话（`PERMISSION_LABELS`：只读 / 可写）。发送成功提示：**"对方登录后在左侧
     「分享给我的」里就能看到并下载"**。
  2. **对方还没有账号？复制链接发给他**：直接生成分享链接（只读、7 天有效）并给出可复制的完整地址；
     链接地址同样走 `publicUrl.ts`，需要提醒时用 `Alert` 显示。
- 接收方侧：别人授权给他的东西出现在左侧「分享给我的」（`SharedWithMePage`），
  文件可直接下载，文件夹可进去浏览。
- 数据绑定复用既有 hooks（`useGrantPermission` / `useCreateShare`），组件不发裸请求；
  错误统一走 `sharingErrorMessage`。

### 2.6 回收站

支持：

- 查看已删除资源。
- 恢复。
- 永久删除。
- 按类型筛选。
- 显示删除时间。
- 恢复冲突提示。

恢复失败与冲突的服务端规则见 [recovery.md](recovery.md)。

### 2.7 管理员页面

管理员可以查看：

- 用户列表。
- 用户状态。
- 存储配额。
- 已使用空间。
- 文件数量。
- 系统存储状态。
- 上传失败统计。
- 审计日志。

**管理员不能在没有审计记录的情况下静默读取用户文件。**

## 3. 必须实现的交互

| # | 交互 |
|---|---|
| 1 | 创建文件夹 |
| 2 | 重命名文件 |
| 3 | 重命名文件夹 |
| 4 | 移动文件和文件夹 |
| 5 | 删除文件 |
| 6 | 恢复文件 |
| 7 | 永久删除文件 |
| 8 | 文件下载 |
| 9 | 历史版本下载 |
| 10 | 历史版本恢复 |
| 11 | 分块上传 |
| 12 | 断点续传 |
| 13 | 暂停上传 |
| 14 | 取消上传 |
| 15 | 失败重试 |
| 16 | 多文件并发上传 |
| 17 | 同名文件冲突提示 |
| 18 | 权限不足提示 |
| 19 | 网络断开提示 |
| 20 | 页面刷新后恢复上传任务 |

## 4. 状态模型

### 4.1 页面状态

必须区分以下状态：

```text
idle
loading
success
error
retrying
offline
cancelled
```

### 4.2 上传状态

```text
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
```

状态枚举在后端的对应关系见 [data-model.md](data-model.md)。

### 4.3 刷新后的恢复

前端刷新页面后，需要根据后端上传会话恢复状态，而不是只依赖内存中的 React 状态。

- 未完成上传任务的必要信息（`upload_id` 和文件基本信息）存 IndexedDB。
- 进入页面时用 `upload_id` 向后端查询真实状态。
- 后端返回的 `completed_chunks` 是过滤已完成分块的依据。

## 5. 上传算法与重试

### 5.1 上传流程

```text
选择文件
    ↓
读取文件大小和名称
    ↓
分块计算 SHA-256
    ↓
调用初始化接口
    ↓
获取 completed_chunks
    ↓
过滤已完成分块
    ↓
限制并发上传分块数量
    ↓
上传缺失分块
    ↓
查询上传状态
    ↓
调用 complete
    ↓
等待服务端校验
    ↓
刷新文件列表
```

前端不能依赖所有分块顺序上传。

### 5.2 并发上限

- `MAX_PARALLEL_CHUNKS` 默认 4。
- 分块大小、分块索引与单文件上限见 [storage.md](storage.md)。

### 5.3 伪代码

```typescript
const session = await createUpload(file);

const pendingChunks = chunks.filter(
  (chunk) => !session.completedChunks.includes(chunk.index),
);

await runWithConcurrencyLimit(
  pendingChunks,
  MAX_PARALLEL_CHUNKS,
  async (chunk) => {
    await uploadChunkWithRetry(session.uploadId, chunk);
  },
);

await completeUpload(session.uploadId);
```

### 5.4 可重试错误

```text
网络断开
连接超时
HTTP 408
HTTP 429
HTTP 502
HTTP 503
HTTP 504
```

### 5.5 不可直接重试错误

```text
401
403
404
409
413
422
HASH_MISMATCH
QUOTA_EXCEEDED
```

### 5.6 重试时序

```text
第 1 次：500 ms
第 2 次：1 s
第 3 次：2 s
```

加少量随机抖动，避免大量请求同时重试。

- 自动重试只针对可重试错误，次数有上限。
- 达到上限后停止自动重试，保留会话并提供手动重试。
- 不可直接重试错误必须直接反馈给用户，不进入自动重试。

## 6. 数据与 Token 处理

- 使用 TanStack Query 管理服务端数据。
- 使用 IndexedDB 保存未完成上传任务的必要信息。
- 不在 LocalStorage 中保存密码。
- 不在 LocalStorage 中保存长期敏感 Token。
- Token 处理方式必须统一，避免多个页面分别实现。
- 刷新后从后端会话恢复状态，而不是只依赖内存中的 React 状态。
- 认证方案与 Token 生命周期见 [api.md](api.md)，Token 安全要求见 [security.md](security.md)。

## 7. 交互与质量约束

- 所有 API 错误必须展示用户可理解的消息。
- 所有破坏性操作需要确认。
- 所有按钮和菜单必须根据权限显示，但后端仍然必须校验权限。
- 页面状态与上传状态必须按第 4 节区分，不得用 loading / error 两个值糊弄。
- 不能假设分块按顺序到达；重试只对可重试错误做，并有次数上限。
- 添加前端单元测试和 Playwright 核心流程测试，清单见 [testing.md](testing.md)。
- 更新 README，添加页面截图或录屏说明。


### 系统管理页（阶段十三）

管理员可见 `/admin`（导航项只对 ADMIN 显示，`navItemsForRole` 是纯函数、有单测）：

- **用户管理**：搜索（用户名/邮箱）、分页、状态标签、禁用/启用（破坏性操作二次确认）、改配额（弹窗表单）。
- **存储统计**：用户数、文件数、版本数、占用、磁盘可用、进行中的上传会话。
- **审计日志**：按操作名筛选、分页；页面明确提示"查看审计日志本身也会被记录"。

红线：页面不展示密码、密码哈希、Token 或敏感配置（后端返回的对象里本来就没有）。
前端的入口隐藏与 403 结果页都只是界面行为——权限一律由后端判定。

## 8. 手机端适配（2026-09-21，用户明确指示）

用户原话：「手机端界面适配：仍然是桌面版布局，手机上能用但挤。**修改，我用的非常的憋屈**」。
按 [AGENTS.md](../AGENTS.md) §14.1，手机上难用属于**产品缺陷**，因此这不是可选优化。

**判定**：`frontend/src/hooks/useIsMobile.ts` 统一给出「窄屏」= AntD `lg`（992px）以下。
不直接用 `Grid.useBreakpoint()` 的原因写在那个文件里：首次测量前断点是 `undefined`，
直接取反会让桌面端先闪一帧手机布局；阈值也只在那一处定义。触控最小高度用同一个文件的
`TOUCH_HEIGHT`（44px），不要在组件里各写一个数字。

| 区域 | 手机上的行为 |
|---|---|
| 应用外壳 `app/AppLayout.tsx` | 不渲染左侧栏；导航收进**抽屉**，顶部**汉堡按钮**打开，点导航项**自动关闭**；顶栏精简为「用户名（省略号）+ 角色标签 + 退出登录」，且整组 `line-height: normal`（否则会继承 Header 的 64px 行高，被 `ellipsis` 的 `vertical-align: bottom` 顶到顶栏底部——实测踩到） |
| 文件页 `pages/FilesPage.tsx` | 列表视图改用 **卡片列表**（`components/FileCardList.tsx`）：名称 + 大小/类型/时间 + 一行操作；工具栏换成可换行 flex，搜索框独占一行 |
| 卡片 `components/FileCardList.tsx` | **「发送给…」保持显眼主按钮**（AGENTS.md §14.3，不可降级）；下载/详情/版本/分享/权限/删除收进「更多」下拉，删除仍走二次确认 |
| 网格 `components/FileGrid.tsx` | 列数自适应（手机 1 列 / 窄屏 2 列 / 宽屏更多）；图标按钮在手机上放大到 44px |
| 详情 `components/FileDetailDrawer.tsx` | 宽度占满（`width="100%"`），操作按钮纵向排列、整行可点 |
| 上传 `components/UploadPanel.tsx` | 任务行可换行：文件名省略号、状态与按钮整组换行，按钮在手机上放大 |

验收：`frontend/tests/e2e/mobile.spec.ts`（Pixel 7 真机视口）覆盖
「汉堡开抽屉 → 点导航自动关 → 上传 → 卡片列表 → 「发送给…」可见 → 无横向滚动」，
并在 **360 / 430px** 两种宽度复验不溢出；截图见 `docs/images/mobile-files.png`、`mobile-nav.png`。