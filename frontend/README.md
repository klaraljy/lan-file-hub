# frontend

**这里目前只有一个占位页，不是应用。**

- 阶段一（工程基础设施）只需要 `frontend/` 目录存在，并让 `docker-compose.yml` 的四个服务能编排起来。
- 真正的 React + TypeScript + Vite + Ant Design 前端在**阶段十**交付，
  页面清单、交互清单与状态模型见 `../docs/frontend.md` 与 `../docs/phases/10-frontend.md`。

当前的 `Dockerfile` 只是把 `index.html` 放进 nginx，不要把它当作前端构建方案。
