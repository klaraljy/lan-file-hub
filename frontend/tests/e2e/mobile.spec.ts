/**
 * 手机端界面（阶段 ②：手机要像一个 App，而不是把桌面版压扁）的端到端验证。
 *
 * 用户原话："这个手机端网页的适配非常的粗糙，感觉根本就没有设计"、"我用的非常的憋屈"。
 * 所以这里按**真实手机视口**（Pixel 7：412×915、isMobile、hasTouch）钉住：
 *   ① 底部三个 Tab（文件 / 分享给我的 / 我的）可见可点，且切换正常；
 *   ② 「我的」页里有最近文件 / 回收站 / 设置 / 退出登录；
 *   ③ 上传入口是右下角悬浮按钮（首屏不再被上传区占满）；
 *   ④ 文件卡片里「发送给…」仍是显眼主按钮，点开是**底部滑出面板**；
 *   ⑤ 全程没有横向滚动（360 / 430px 也复验）。
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

test('手机端：底部 Tab / 我的页 / 悬浮上传 / 发送给…底部面板 / 不横向滚动', async ({ page }) => {
  // 1) 注册一个全新用户（避免与既有数据冲突）
  await page.goto('/register');
  await page.getByLabel('用户名').fill(USERNAME);
  await page.getByLabel('邮箱').fill(EMAIL);
  await page.getByLabel('密码', { exact: true }).fill(PASSWORD);
  await page.getByLabel('确认密码').fill(PASSWORD);
  await page.getByRole('button', { name: buttonName('注册') }).click();
  await expect(page).toHaveURL(/\/files/);

  // 2) ① 底部三个 Tab 都可见；② 切换 Tab 正常
  const filesTab = page.getByRole('button', { name: '文件', exact: true });
  const sharedTab = page.getByRole('button', { name: '分享给我的', exact: true });
  const meTab = page.getByRole('button', { name: '我的', exact: true });
  await expect(filesTab).toBeVisible();
  await expect(sharedTab).toBeVisible();
  await expect(meTab).toBeVisible();

  await sharedTab.click();
  await expect(page).toHaveURL(/\/shared/);
  await filesTab.click();
  await expect(page).toHaveURL(/\/files/);

  // 2.1) 用户 2026-10-03 实测："点分享给我的，导航栏直接消失"。
  //      真因是这些页面用了电脑版宽表格 → 页面被撑得比屏幕宽 → 横滑时固定底栏被推出可视区。
  //      手机上改成卡片列表后，这里把"底栏始终在可视区内 + 页面不横向溢出"钉死。
  await sharedTab.click();
  await expect(page).toHaveURL(/\/shared/);
  await expect(sharedTab).toBeVisible();
  {
    const navBox = await page.locator('nav[aria-label="主导航"]').boundingBox();
    const viewport = page.viewportSize();
    expect(navBox, '底栏必须还在').not.toBeNull();
    expect(viewport).not.toBeNull();
    expect(navBox!.y, '底栏跑到屏幕上方了').toBeGreaterThanOrEqual(0);
    expect(
      navBox!.y + navBox!.height,
      '底栏被挤出可视区（就是用户报的"导航栏消失"）',
    ).toBeLessThanOrEqual(viewport!.height + 1);
  }
  expect(await horizontalOverflow(page), '「分享给我的」不该横向溢出').toBeLessThanOrEqual(1);
  await page.screenshot({ path: join(SHOT_DIR, 'mobile-shared.png'), fullPage: false });

  // 「回收站」同样要满足（它原来是宽表格，手机上已换卡片）
  await meTab.click();
  await page.getByText('回收站').click();
  await expect(page).toHaveURL(/\/trash/);
  {
    const navBox = await page.locator('nav[aria-label="主导航"]').boundingBox();
    const viewport = page.viewportSize();
    expect(navBox).not.toBeNull();
    expect(viewport).not.toBeNull();
    expect(navBox!.y + navBox!.height).toBeLessThanOrEqual(viewport!.height + 1);
  }
  expect(await horizontalOverflow(page), '「回收站」不该横向溢出').toBeLessThanOrEqual(1);
  await filesTab.click();
  await expect(page).toHaveURL(/\/files/);

  // 3) 「我的」页：最近文件 / 回收站 / 设置 / 退出登录 都在这里
  await meTab.click();
  await expect(page).toHaveURL(/\/me/);
  await expect(page.getByText('最近文件')).toBeVisible();
  await expect(page.getByText('回收站')).toBeVisible();
  await expect(page.getByText('设置')).toBeVisible();
  await expect(page.getByRole('button', { name: buttonName('退出登录') })).toBeVisible();
  await page.screenshot({ path: join(SHOT_DIR, 'mobile-my.png'), fullPage: false });

  // 回到「文件」
  await filesTab.click();
  await expect(page).toHaveURL(/\/files/);

  // 4) ③ 上传入口是右下角悬浮按钮；选文件后走同一套上传逻辑（隐藏 input）
  const uploadButton = page.getByRole('button', { name: buttonName('上传') });
  await expect(uploadButton).toBeVisible();
  await page.locator('input[type="file"][multiple]').setInputFiles({
    name: SAMPLE_NAME,
    mimeType: 'text/plain',
    buffer: Buffer.from('mobile e2e\n', 'utf-8'),
  });
  await expect(page.getByText(SAMPLE_NAME).first()).toBeVisible({ timeout: 30_000 });

  // 5) ④ 文件卡片里「发送给…」是显眼主按钮（AGENTS.md §14.3）
  const sendButton = page.getByRole('button', { name: /发送给/ }).first();
  await expect(sendButton).toBeVisible();
  await expect(sendButton).toHaveClass(/ant-btn-primary/);

  // 6) ⑤ 没有横向滚动
  expect(await horizontalOverflow(page)).toBeLessThanOrEqual(1);
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.waitForTimeout(300);
  await page.screenshot({ path: join(SHOT_DIR, 'mobile-files.png'), fullPage: false });

  // 7) 「发送给…」在手机上是从底部滑出的面板（阶段 ② 的硬要求），内容仍完整
  await sendButton.click();
  await expect(page.locator('.ant-drawer-content')).toBeVisible();
  await expect(page.getByText('对方登录后，在左侧「分享给我的」里就能看到并下载。')).toBeVisible();
  await expect(page.getByText('对方还没有账号？')).toBeVisible();
  // 等滑出动画结束再截图/量层级，否则会截到动画中间态
  await page.waitForTimeout(400);
  const layers = await page.evaluate(() => {
    const zIndex = (selector: string) => {
      const element = document.querySelector(selector);
      return element ? getComputedStyle(element).zIndex : 'missing';
    };
    return {
      mask: zIndex('.ant-drawer-mask'),
      drawer: zIndex('.ant-drawer-content-wrapper'),
      tabBar: zIndex('nav[aria-label="主导航"]'),
    };
  });
  console.log('面板/底栏层级：', JSON.stringify(layers));
  // 底部 Tab 与悬浮上传条必须在滑出面板**下面**（否则会盖住面板按钮）
  expect(Number(layers.drawer)).toBeGreaterThan(Number(layers.tabBar));
  await page.screenshot({ path: join(SHOT_DIR, 'mobile-sheet.png'), fullPage: false });
  expect(await horizontalOverflow(page)).toBeLessThanOrEqual(1);
  await page.keyboard.press('Escape');

  // 8) 用户反馈的区间是 360–430px：两端宽度都不能横向滚动，「发送给…」也要还在
  for (const width of [360, 430]) {
    await page.setViewportSize({ width, height: 800 });
    await page.waitForTimeout(300);
    expect(await horizontalOverflow(page), `${width}px 宽出现横向滚动`).toBeLessThanOrEqual(1);
    await expect(page.getByRole('button', { name: /发送给/ }).first()).toBeVisible();
    await expect(filesTab).toBeVisible();
  }
});
