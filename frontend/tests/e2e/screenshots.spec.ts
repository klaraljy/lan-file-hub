/**
 * 真实界面截图（阶段十六第 17 条）。
 *
 * 不是画的、也不是伪造的：用真实浏览器打开真实服务，截真实页面，
 * 输出到 docs/images/，README 直接引用。
 *
 * 前置（与 core-flow.spec.ts 相同）：
 *   PostgreSQL/Redis 在跑，后端在 8080，前端 dev server 在 5173，
 *   并且已经准备好演示数据（用户、文件夹、文件、分享链接、管理员）。
 *
 * 执行：
 *   $env:PLAYWRIGHT_SHOTS='1'; npx playwright test tests/e2e/screenshots.spec.ts
 */
import { expect, test } from '@playwright/test';
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';

const ENABLED = process.env.PLAYWRIGHT_SHOTS === '1';
test.skip(!ENABLED, '未设置 PLAYWRIGHT_SHOTS=1');

const SHOT_DIR = join('..', 'docs', 'images');
const DEMO_USER = process.env.SHOT_DEMO_USER ?? 'demo728';
const DEMO_PASSWORD = process.env.SHOT_DEMO_PASSWORD ?? 'Demo-Password-9!';
const ADMIN_USER = process.env.SHOT_ADMIN_USER ?? 'shotadmin';
const ADMIN_PASSWORD = process.env.SHOT_ADMIN_PASSWORD ?? 'Screenshot-Admin-Password-9!';
const SHARE_TOKEN = process.env.SHOT_SHARE_TOKEN ?? '';

test.beforeAll(() => {
  mkdirSync(SHOT_DIR, { recursive: true });
});

async function shot(page: import('@playwright/test').Page, name: string) {
  // 关掉动画，避免截到过渡中间态
  await page.addStyleTag({ content: '*{transition:none !important;animation:none !important}' });
  await page.waitForTimeout(400);
  await page.screenshot({ path: join(SHOT_DIR, `${name}.png`), fullPage: false });
}

async function login(page: import('@playwright/test').Page, username: string, password: string) {
  await page.goto('/login');
  await page.getByLabel('用户名').fill(username);
  await page.getByLabel('密码', { exact: true }).fill(password);
  await page.getByRole('button', { name: /登\s*录/ }).click();
  await expect(page).toHaveURL(/\/files/, { timeout: 20_000 });
}

test('登录页', async ({ page }) => {
  await page.goto('/login');
  await shot(page, '01-login');
});

test('注册页', async ({ page }) => {
  await page.goto('/register');
  await shot(page, '02-register');
});

test('我的文件（文件夹 + 文件列表）', async ({ page }) => {
  await login(page, DEMO_USER, DEMO_PASSWORD);
  await expect(page.getByRole('row').first()).toBeVisible({ timeout: 20_000 });
  await shot(page, '03-files');
});

test('上传面板', async ({ page }) => {
  await login(page, DEMO_USER, DEMO_PASSWORD);
  const toggle = page.getByRole('button', { name: /上传/ }).first();
  if (await toggle.isVisible().catch(() => false)) {
    await toggle.click();
    await page.waitForTimeout(600);
  }
  await shot(page, '04-upload');
});

test('回收站', async ({ page }) => {
  await login(page, DEMO_USER, DEMO_PASSWORD);
  await page.getByRole('menuitem', { name: '回收站' }).click();
  await expect(page).toHaveURL(/\/trash/);
  await page.waitForTimeout(800);
  await shot(page, '05-trash');
});

test('分享给我的', async ({ page }) => {
  await login(page, DEMO_USER, DEMO_PASSWORD);
  await page.getByRole('menuitem', { name: '分享给我的' }).click();
  await expect(page).toHaveURL(/\/shared/);
  await page.waitForTimeout(800);
  await shot(page, '06-shared');
});

test('系统管理（管理员）', async ({ page }) => {
  await login(page, ADMIN_USER, ADMIN_PASSWORD);
  await page.goto('/admin');
  await expect(page.getByRole('heading', { name: '系统管理' })).toBeVisible({ timeout: 20_000 });
  await page.waitForTimeout(1200); // 等表格数据
  await shot(page, '07-admin-users');
  await page.getByRole('tab', { name: '审计日志' }).click();
  await page.waitForTimeout(1200);
  await shot(page, '08-admin-audit');
});

test('公开分享页（无需登录）', async ({ page }) => {
  test.skip(!SHARE_TOKEN, '未提供 SHOT_SHARE_TOKEN');
  await page.goto(`/s/${SHARE_TOKEN}`);
  await expect(page.getByText(/下载|分享/).first()).toBeVisible({ timeout: 20_000 });
  await shot(page, '09-public-share');
});

test('指标端点（文本）', async ({ page }) => {
  await page.goto('http://127.0.0.1:8080/metrics');
  await expect(page.locator('body')).toContainText('lan_file_hub_http_requests_total');
  await shot(page, '10-metrics');
});
