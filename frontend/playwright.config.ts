/**
 * Playwright 配置（端到端测试脚手架，阶段十）。
 *
 * ⚠️ 本机**无法执行**这些用例，原因见 tests/e2e/core-flow.spec.ts 头部注释：
 *    浏览器二进制没有安装（`npx playwright install` 需要访问外网，当前代理已关闭），
 *    并且 @playwright/test 也没有装进 node_modules。
 *    因此这份配置与用例是「接线完成、待执行」的状态，不是「已通过」。
 *
 * 等能联网时按下面三步即可跑起来：
 *   1. cd frontend && npm i -D @playwright/test && npx playwright install chromium
 *   2. docker compose up -d postgres redis，启动后端与前端 dev server
 *   3. PLAYWRIGHT_E2E=1 npx playwright test
 *
 * 不在 webServer 里自动起服务：本项目的后端是 C++ 进程 + PostgreSQL + Redis，
 * 由开发者按 README 的步骤启动更稳妥，避免测试脚本悄悄拉起一整套依赖。
 */
import { defineConfig, devices } from '@playwright/test';

export default defineConfig({
  testDir: './tests/e2e',
  // 端到端用例会真的写数据，串行执行更好定位问题
  fullyParallel: false,
  workers: 1,
  forbidOnly: Boolean(process.env.CI),
  retries: 0,
  reporter: [['list']],
  timeout: 60_000,
  expect: { timeout: 10_000 },

  use: {
    // 与 vite dev server 的默认端口一致
    baseURL: process.env.E2E_BASE_URL ?? 'http://localhost:5173',
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'off',
  },

  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],
});
