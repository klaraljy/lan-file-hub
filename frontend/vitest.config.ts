import { defineConfig } from 'vitest/config';

// 只放测试相关配置。构建配置在 vite.config.ts（vitest 会优先读本文件）。
export default defineConfig({
  test: {
    environment: 'jsdom',
    globals: true,
    include: ['tests/**/*.test.{ts,tsx}'],
  },
});
