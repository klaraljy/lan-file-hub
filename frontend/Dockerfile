# 前端镜像（多阶段构建）。
# 阶段二只有登录/注册/占位主页；完整界面在阶段十。

# ---- 构建阶段 ----
FROM node:22-alpine AS builder

WORKDIR /app

# 先装依赖，利用层缓存
COPY package.json package-lock.json* ./
RUN npm ci --no-fund --no-audit || npm install --no-fund --no-audit

COPY tsconfig.json vite.config.ts index.html ./
COPY src ./src
RUN npm run build

# ---- 运行阶段 ----
FROM nginx:1.27-alpine AS runtime

COPY --from=builder /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80
