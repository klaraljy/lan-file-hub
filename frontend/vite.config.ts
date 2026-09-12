import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// 开发时把 /api 代理到后端，避免跨域；生产环境由反向代理负责（见 frontend/nginx.conf）。
// 注意：测试配置放在 vitest.config.ts —— vitest 自带一份 vite，
// 把 test 字段写在这里会让 plugins 的类型对不上。
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': { target: 'http://127.0.0.1:8080', changeOrigin: true },
      '/health': { target: 'http://127.0.0.1:8080', changeOrigin: true },
    },
  },
  build: {
    outDir: 'dist',
    sourcemap: false,
  },
});
