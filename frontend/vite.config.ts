import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// 开发时把 /api 代理到后端，避免跨域；生产环境由反向代理负责（见 frontend/nginx.conf）。
// 后端地址以 LFH_BACKEND_URL 为准（scripts/start.ps1 按 .env 的 HTTP_PORT 注入），
// 默认 8080。注意：本机 8080 可能落在 Windows 保留端口段里（Docker Desktop 会预留），
// 那时改 .env 的 HTTP_PORT 即可，前端不用动。
const backendUrl = process.env.LFH_BACKEND_URL ?? 'http://127.0.0.1:8080';

// 注意：测试配置放在 vitest.config.ts —— vitest 自带一份 vite，
// 把 test 字段写在这里会让 plugins 的类型对不上。
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': { target: backendUrl, changeOrigin: true },
      '/health': { target: backendUrl, changeOrigin: true },
    },
  },
  build: {
    outDir: 'dist',
    sourcemap: false,
  },
});
