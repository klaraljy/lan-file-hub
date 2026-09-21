/**
 * 手机端界面适配的端到端验证（AGENTS.md §14.1：手机上"憋屈" = 产品缺陷）。
 *
 * 用 Playwright 的**真机设备描述**（Pixel 7：412×915、isMobile、hasTouch）跑完整路径：
 *   注册 → 我的文件 → 上传 → 卡片列表 → 「发送给…」→ 导航抽屉 → 无横向滚动，
 * 并把手机版首页截图存到 docs/images/mobile-files.png 供人工确认。
 *
 * 执行（服务需已在运行：后端 9080、前端 5173）：
 *   cd frontend
 *   $env:PLAYWRIGHT_E2E='1'; npx playwright test mobile --reporter=line
 */
import { expect, test, devices } from '@playwright/test';
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';

const ENABLED = process.env.PLAYWRIGHT_E2E === '1';
test.skip(!ENABLED, '未设置 PLAYWRIGHT_E2E=1（需要先安装 Playwright 浏览器与后端服务）');

// 真机视口：Pixel 7 属于用户反馈的 360–430px 这一档
test.use({ ...devices['Pixel 7'] });

const USERNAME = process.env.E2E_MOBILE_USERNAME ?? `e2emobile${Date.now()}`;
const EMAIL = `${USERNAME}@example.com`;
const PASSWORD = 'correct-horse-battery';
const SAMPLE_NAME = 'mobile-sample.txt';
const SHOT_DIR = join('..', 'docs', 'images');

/** Ant Design 会给「两个汉字」的按钮插入空格（注册 → 注 册） */
function buttonName(text: string): RegExp {
  return new RegExp(text.split('').join('\\s*'));
}

/** 页面级横向溢出量（>1px 就说明手机上要左右拖动才能看全） */
async function horizontalOverflow(page: import('@playwright/test').Page): Promise<number> {
  return page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
}

test.beforeAll(() => {
  mkdirSync(SHOT_DIR, { recursive: true });
});

test('手机端：导航抽屉 / 卡片列表 / 发送给… / 没有横向滚动', async ({ page }) => {
  // 1) 注册一个全新用户（避免与既有数据冲突）
  await page.goto('/register');
  await page.getByLabel('用户名').fill(USERNAME);
  await page.getByLabel('邮箱').fill(EMAIL);
  await page.getByLabel('密码', { exact: true }).fill(PASSWORD);
  await page.getByLabel('确认密码').fill(PASSWORD);
  await page.getByRole('button', { name: buttonName('注册') }).click();
  await expect(page).toHaveURL(/\/files/);

  // 2) ① 汉堡按钮可见，点开后导航可见；点导航项应**自动关闭**抽屉
  const hamburger = page.getByRole('button', { name: '打开导航菜单' });
  await expect(hamburger).toBeVisible();
  await hamburger.click();
  await expect(page.getByRole('menuitem', { name: '分享给我的' })).toBeVisible();
  // 截图：手机版导航抽屉
  await page.addStyleTag({ content: '*{transition:none !important;animation:none !important}' });
  await page.waitForTimeout(300);
  await page.screenshot({ path: join(SHOT_DIR, 'mobile-nav.png'), fullPage: false });
  await page.getByRole('menuitem', { name: '回收站' }).click();
  await expect(page).toHaveURL(/\/trash/);
  await expect(page.getByRole('menuitem', { name: '分享给我的' })).toBeHidden();

  // 回到「我的文件」
  await hamburger.click();
  await page.getByRole('menuitem', { name: '我的文件' }).click();
  await expect(page).toHaveURL(/\/files/);

  // 3) 上传一个文件（手机路径里必须能上传）
  await page.setInputFiles('.ant-upload input[type="file"]', {
    name: SAMPLE_NAME,
    mimeType: 'text/plain',
    buffer: Buffer.from('mobile e2e\n', 'utf-8'),
  });

  // ② 卡片列表里能看到文件（手机上是卡片而不是宽表格）
  await expect(page.getByText(SAMPLE_NAME).first()).toBeVisible({ timeout: 30_000 });
  // ③ 「发送给…」必须是显眼的主按钮（AGENTS.md §14.3）
  const sendButton = page.getByRole('button', { name: /发送给/ }).first();
  await expect(sendButton).toBeVisible();

  // ④ 没有横向滚动
  expect(await horizontalOverflow(page)).toBeLessThanOrEqual(1);

  // 截图：手机版「我的文件」（回到顶部，让顶栏完整入镜），供人工确认排版
  await page.addStyleTag({ content: '*{transition:none !important;animation:none !important}' });
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.waitForTimeout(400);
  await page.screenshot({ path: join(SHOT_DIR, 'mobile-files.png'), fullPage: false });

  // 4) 「发送给…」弹窗在手机宽度下也要能用，且不产生横向滚动
  await sendButton.click();
  await expect(page.getByText('对方登录后，在左侧「分享给我的」里就能看到并下载。')).toBeVisible();
  expect(await horizontalOverflow(page)).toBeLessThanOrEqual(1);

  // 5) 用户反馈的区间是 360–430px：两端宽度都不能出现横向滚动，「发送给…」也要还在
  for (const width of [360, 430]) {
    await page.keyboard.press('Escape');
    await page.setViewportSize({ width, height: 800 });
    await page.waitForTimeout(300);
    expect(await horizontalOverflow(page), `${width}px 宽出现横向滚动`).toBeLessThanOrEqual(1);
    await expect(page.getByRole('button', { name: /发送给/ }).first()).toBeVisible();
  }
});
