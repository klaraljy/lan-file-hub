import { defineConfig } from 'vitest/config';

// 只放测试相关配置。构建配置在 vite.config.ts（vitest 会优先读本文件）。
export default defineConfig({
  test: {
    environment: 'jsdom',
    globals: true,
    include: ['tests/**/*.test.{ts,tsx}'],
    // 组件测试要真的渲染 AntD（`tests/rowActionsView.test.tsx`），垫片见 tests/setup.ts
    setupFiles: ['./tests/setup.ts'],
  },
});
