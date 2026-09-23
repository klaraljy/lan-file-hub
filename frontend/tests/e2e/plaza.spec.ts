/**
 * 「广场」的端到端验证（五阶段改造 ⑤，AGENTS.md §15）。
 *
 * 覆盖：手机端底部 Tab 有「广场」→ 页面能打开（谁在线 / 我的设备 / 空状态文案）→
 *       设置里能切换「隐身」，并真的发出 `PUT /api/v1/presence/visibility`。
 *
 * 执行（服务需已在运行：后端 9080、前端 5173）：
 *   cd frontend
 *   $env:PLAYWRIGHT_E2E='1'; npx playwright test plaza --reporter=line
 */
import { expect, test, devices } from '@playwright/test';

const ENABLED = process.env.PLAYWRIGHT_E2E === '1';
test.skip(!ENABLED, '未设置 PLAYWRIGHT_E2E=1（需要先安装 Playwright 浏览器与后端服务）');

// 用户反馈的手机宽度区间（360–430px），Pixel 7 是其中之一
test.use({ ...devices['Pixel 7'] });

const USERNAME = process.env.E2E_PLAZA_USERNAME ?? `e2eplaza${Date.now()}`;
const EMAIL = `${USERNAME}@example.com`;
const PASSWORD = 'correct-horse-battery';

/** Ant Design 会给「两个汉字」的按钮插入空格（注册 → 注 册） */
function buttonName(text: string): RegExp {
  return new RegExp(text.split('').join('\\s*'));
}

test('手机端：广场入口 → 页面 → 隐身开关', async ({ page }) => {
  // 1) 注册一个全新用户（避免与既有数据冲突）
  await page.goto('/register');
  await page.getByLabel('用户名').fill(USERNAME);
  await page.getByLabel('邮箱').fill(EMAIL);
  await page.getByLabel('密码', { exact: true }).fill(PASSWORD);
  await page.getByLabel('确认密码').fill(PASSWORD);
  await page.getByRole('button', { name: buttonName('注册') }).click();
  await expect(page).toHaveURL(/\/files/);

  // 2) 底部 Tab 里有「广场」，点进去
  const plazaTab = page.getByRole('button', { name: '广场', exact: true });
  await expect(plazaTab).toBeVisible();
  await plazaTab.click();
  await expect(page).toHaveURL(/\/plaza/);

  // 3) 页面内容：谁在线 + 我的设备（同一账号不需要互传）
  await expect(page.getByText('谁在线')).toBeVisible();

  // 「谁在线」里**不该出现自己**（自己看「我的设备」）——
  // 不假设"此刻只有我一个人在线"：同一台机器上其它会话留下的在线记录会正常出现在这里
  const onlineCard = page.locator('.ant-card', { hasText: '谁在线' });
  await expect(onlineCard.getByText(USERNAME)).toHaveCount(0);
  // 要么是空状态，要么至少有一张卡带「发文件给他」按钮
  const emptyHint = onlineCard.getByText(/现在没有别人在线/);
  const sendButtons = onlineCard.getByRole('button', { name: /发文件给他/ });
  await expect
    .poll(async () => (await emptyHint.count()) + (await sendButtons.count()))
    .toBeGreaterThan(0);

  await expect(page.getByText('同一个账号的文件本来就是同一份，不需要互传')).toBeVisible();
  // 手机上不能出现横向滚动
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
  expect(overflow).toBeLessThanOrEqual(1);

  // 4) 设置里能切换隐身，而且要真的发请求
  //    ⚠️ 必须 exact：否则「我的」会同时匹配到底部 Tab 与「我的文件」等含该词的元素
  await page.getByRole('button', { name: '我的', exact: true }).click();
  await expect(page).toHaveURL(/\/me/);
  await page.getByText('设置', { exact: true }).click();
  await expect(page).toHaveURL(/\/settings/);

  // 默认可见：文案是"让别人看到我在线"
  await expect(page.getByText('让别人看到我在线')).toBeVisible();

  const hideResponse = page.waitForResponse(
    (response) => response.url().includes('/presence/visibility') && response.request().method() === 'PUT',
  );
  await page.getByRole('switch').click();
  expect((await hideResponse).status()).toBe(200);
  await expect(page.getByText('隐藏我的在线状态')).toBeVisible();

  const showResponse = page.waitForResponse(
    (response) => response.url().includes('/presence/visibility') && response.request().method() === 'PUT',
  );
  await page.getByRole('switch').click();
  expect((await showResponse).status()).toBe(200);
  await expect(page.getByText('让别人看到我在线')).toBeVisible();

  // 5) 桌面端左侧导航也有「广场」（手机上验证完，再确认宽屏不会被漏掉）
  await page.setViewportSize({ width: 1280, height: 900 });
  await expect(page.getByRole('menuitem', { name: '广场' })).toBeVisible();
});
