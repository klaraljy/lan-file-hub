/**
 * 核心用户路径的端到端测试（docs/testing.md §四）。
 *
 * 执行前置（阶段十四已真实执行，结果见 docs/performance.md「端到端」）：
 *   1. `npm i -D @playwright/test && npx playwright install chromium`；
 *   2. PostgreSQL 与 Redis 在跑，后端已启动（默认 127.0.0.1:8080）；
 *   3. 前端 dev server 在 5173（vite 把 /api 与 /health 代理到后端）。
 *
 * 执行方式：
 *   cd frontend
 *   npm i -D @playwright/test && npx playwright install chromium
 *   docker compose up -d postgres redis          # 依赖服务
 *   .\build\debug\bin\lan_file_hub_server.exe    # 后端
 *   npm run dev                                  # 前端
 *   $env:PLAYWRIGHT_E2E='1'; npx playwright test
 *
 * 没有设置 PLAYWRIGHT_E2E=1 时全部用例会被跳过，避免在没准备环境时误报失败。
 */
import { expect, test } from '@playwright/test';

const ENABLED = process.env.PLAYWRIGHT_E2E === '1';

// 没有显式开启就跳过：本机（无浏览器二进制）默认走这条路
test.skip(!ENABLED, '未设置 PLAYWRIGHT_E2E=1（需要先安装 Playwright 浏览器与后端服务）');

const USERNAME = process.env.E2E_USERNAME ?? `e2e${Date.now()}`;
const EMAIL = process.env.E2E_EMAIL ?? `${USERNAME}@example.com`;
const PASSWORD = process.env.E2E_PASSWORD ?? 'correct-horse-battery';

/** 上传用的临时文件内容；用文本文件避免依赖二进制夹具 */
const SAMPLE_NAME = 'e2e-sample.txt';
const SAMPLE_CONTENT = 'lan-file-hub e2e 内容\n';


/**
 * Ant Design 会给「两个汉字」的按钮插入空格（注册 → 注 册），
 * 所以按钮名匹配必须容忍中间的空格。
 */
function buttonName(text: string): RegExp {
  return new RegExp(text.split('').join('\\s*'));
}

test.describe('核心流程', () => {
  test('注册 → 浏览目录 → 上传 → 下载 → 删除 → 回收站恢复 → 创建分享', async ({ page }) => {
  /** 文件行定位：文件名会同时出现在上传任务面板与列表里，必须限定到列表行 */
  const fileRow = () => page.getByRole('row', { name: new RegExp(SAMPLE_NAME) });
    // 1) 注册（新用户，避免与既有数据冲突）
    await page.goto('/register');
    await page.getByLabel('用户名').fill(USERNAME);
    await page.getByLabel('邮箱').fill(EMAIL);
    await page.getByLabel('密码', { exact: true }).fill(PASSWORD);
    await page.getByLabel('确认密码').fill(PASSWORD);
    await page.getByRole('button', { name: buttonName('注册') }).click();

    // 2) 进入「我的文件」，应自动落到根目录
    await expect(page).toHaveURL(/\/files/);
    await expect(page.getByText('我的文件').first()).toBeVisible();

    // 3) 上传一个文件：UploadPanel 里的原生 file input
    await page.setInputFiles('input[type="file"]', {
      name: SAMPLE_NAME,
      mimeType: 'text/plain',
      buffer: Buffer.from(SAMPLE_CONTENT, 'utf-8'),
    });
    // 上传完成后文件出现在列表里
    await expect(fileRow().first()).toBeVisible({ timeout: 30_000 });

    // 4) 下载：点击行内「下载」，断言浏览器收到下载事件
    const downloadPromise = page.waitForEvent('download');
    await page.getByRole('button', { name: buttonName('下载') }).first().click();
    const download = await downloadPromise;
    expect(download.suggestedFilename()).toBe(SAMPLE_NAME);

    // 5) 删除（软删除，需要二次确认）
    await page.getByRole('button', { name: buttonName('删除') }).first().click();
    await page.getByRole('button', { name: buttonName('删除') }).last().click(); // Popconfirm 的确认按钮
    await expect(fileRow()).toHaveCount(0);

    // 6) 回收站里能看到，并恢复
    await page.getByRole('menuitem', { name: '回收站' }).click();
    await expect(fileRow().first()).toBeVisible();
    await page.getByRole('button', { name: buttonName('恢复') }).first().click();
    await page.getByRole('button', { name: buttonName('恢复') }).last().click(); // 二次确认
    await expect(fileRow()).toHaveCount(0);

    // 7) 回到「我的文件」，文件已恢复
    await page.getByRole('menuitem', { name: '我的文件' }).click();
    await expect(fileRow().first()).toBeVisible();

    // 8) 创建分享链接，并断言明文 Token 展示了一次
    await page.getByRole('button', { name: buttonName('分享') }).first().click();
    await page.getByRole('button', { name: '创建链接' }).click();
    await expect(page.getByText('明文 Token 只显示这一次')).toBeVisible();
    // 弹窗里有多个只读输入框（权限、次数、链接），逐个取值断言"其中一个"是分享链接
    const readonlyValues = await page
      .locator('input[readonly]')
      .evaluateAll((elements) => elements.map((element) => (element as HTMLInputElement).value));
    expect(readonlyValues.some((value) => value.startsWith('http') && value.includes('/s/'))).toBe(true);
  });

  test('公开分享页不需要登录即可打开', async ({ page, context }) => {
    // 用干净的上下文模拟「陌生人打开链接」：没有任何 Token
    await context.clearCookies();
    await page.goto('/s/definitely-not-a-real-token');
    // 失效链接必须给出可理解的原因，而不是白屏或英文报错
    await expect(page.getByText('无法打开这个分享', { exact: true })).toBeVisible();
    await expect(page.getByText(/不存在|过期|撤销/).first()).toBeVisible();
  });
});
