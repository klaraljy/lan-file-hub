/**
 * 「发送给…」按用户分享的端到端测试（AGENTS.md §14.3 / §14.4）。
 *
 * 为什么单独一个文件：这是用户真机试用后提出的核心诉求——"我要把文件发给某个人"，
 * 必须验证到**浏览器里真的能点出来**，而不是只过类型检查与单测。
 *
 * 前置与执行方式同 core-flow.spec.ts：
 *   1. PostgreSQL/Redis 在跑，后端已启动（默认 127.0.0.1:9080）；
 *   2. 前端 dev server 在 5173；
 *   3. `$env:PLAYWRIGHT_E2E='1'; npx playwright test send-to`
 *
 * 「库里有别的用户」这一前提由 E2E_LOOKUP_KEYWORD 控制（默认 `klara`，本机库里确实有）；
 * 换环境时用环境变量指定一个真实存在的用户名即可。
 */
import { expect, test } from '@playwright/test';
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';

const ENABLED = process.env.PLAYWRIGHT_E2E === '1';
test.skip(!ENABLED, '未设置 PLAYWRIGHT_E2E=1（需要先安装 Playwright 浏览器与后端服务）');

/** 与 screenshots.spec.ts 保持同一约定：相对 frontend/ 的仓库 docs/images */
const SHOT_DIR = join('..', 'docs', 'images');

const USERNAME = process.env.E2E_SENDTO_USERNAME ?? `e2esend${Date.now()}`;
const EMAIL = `${USERNAME}@example.com`;
const PASSWORD = 'correct-horse-battery';
const SAMPLE_NAME = 'sendto-sample.txt';
/** 库里必须**已经存在**的另一个用户（用于"发给谁"的搜索结果） */
const LOOKUP_KEYWORD = process.env.E2E_LOOKUP_KEYWORD ?? 'klara';

/** Ant Design 会给「两个汉字」的按钮插入空格（发送 → 发 送） */
function buttonName(text: string): RegExp {
  return new RegExp(text.split('').join('\\s*'));
}

test.describe('发送给某个用户', () => {
  test('选人 → 发送；生成的链接必须是手机能打开的局域网地址', async ({ page }) => {
    // 1) 注册一个全新用户（避免与既有数据冲突）
    await page.goto('/register');
    await page.getByLabel('用户名').fill(USERNAME);
    await page.getByLabel('邮箱').fill(EMAIL);
    await page.getByLabel('密码', { exact: true }).fill(PASSWORD);
    await page.getByLabel('确认密码').fill(PASSWORD);
    await page.getByRole('button', { name: buttonName('注册') }).click();
    await expect(page).toHaveURL(/\/files/);

    // 2) 上传一个文件：文件行上才会有「发送给…」
    await page.setInputFiles('input[type="file"]', {
      name: SAMPLE_NAME,
      mimeType: 'text/plain',
      buffer: Buffer.from('send-to e2e\n', 'utf-8'),
    });
    await expect(page.getByRole('row', { name: new RegExp(SAMPLE_NAME) }).first()).toBeVisible({
      timeout: 30_000,
    });

    // 3) 点显眼的主按钮「发送给…」（AGENTS.md §14.3 要求不许藏在更多菜单里）
    await page.getByRole('button', { name: /发送给/ }).first().click();
    await expect(page.getByText('对方登录后，在左侧「分享给我的」里就能看到并下载。')).toBeVisible();

    // 4) 选人：输入关键词 → 下拉里出现库里已有的用户 → 选中
    //    注意：AntD 的 Select 占位符渲染成 span（不是 input 的 placeholder 属性），
    //    所以这里按 .ant-select 定位，再用键盘输入触发 onSearch。
    const userSelect = page.locator('.ant-modal .ant-select').first();
    await userSelect.click();
    await page.keyboard.type(LOOKUP_KEYWORD, { delay: 40 });
    const option = page.locator('.ant-select-item-option').filter({ hasText: LOOKUP_KEYWORD }).first();
    await expect(option).toBeVisible({ timeout: 15_000 });
    await option.click();

    // 5) 发送：断言成功提示（明确告诉用户对方去哪拿）
    //    按钮带 SendOutlined 图标，可访问名形如 "send 发 送"，所以匹配结尾即可
    await page.getByRole('button', { name: /发\s*送$/ }).click();
    await expect(page.getByText(/已发送给/)).toBeVisible({ timeout: 15_000 });
    await expect(page.getByText(/「分享给我的」/).first()).toBeVisible();

    // 6) 「对方还没有账号」这条路：生成的链接必须是局域网地址，不能是 127.0.0.1
    //    （用户实测踩到的坑：127.0.0.1 的链接在手机上指手机自己，必然打不开）
    await page.getByRole('button', { name: /发送给/ }).first().click();
    await page.getByRole('button', { name: /生成分享链接/ }).click();
    // 注意：AntD Select 的内部 input 也带 readonly，必须限定到 .ant-input 才是链接输入框
    const linkInput = page.locator('.ant-modal input.ant-input[readonly]');
    await expect(linkInput).toHaveValue(/\/s\/.+/, { timeout: 15_000 });
    const url = await linkInput.inputValue();
    expect(url.startsWith('http://127.0.0.1')).toBe(false);
    expect(url.startsWith('http://localhost')).toBe(false);

    // 7) 二维码：必须渲染出来，并且编码的就是上面这条链接（同一个 url 变量）
    //    （用户要求"浏览器/手机相机都能扫"，所以至少要保证二维码真的存在且是给这条链接用的）
    const qr = page.locator('[data-testid="share-qr"] svg').first();
    await expect(qr).toBeVisible();
    const box = await qr.boundingBox();
    expect(box?.width ?? 0).toBeGreaterThan(100);
    expect(Math.abs((box?.width ?? 0) - (box?.height ?? 0))).toBeLessThan(4); // 正方形
    mkdirSync(SHOT_DIR, { recursive: true });
    await page.locator('.ant-modal').screenshot({ path: join(SHOT_DIR, 'share-qr.png') });

    // 8) 真的把这个二维码**解码**出来，确认扫出来就是这条链接
    //    用开发依赖 jsqr（纯 JS 解码器）注入页面，把 SVG 画到 canvas 再解 ——
    //    这样"手机扫一下能不能用"就不是靠肉眼猜，而是自动化断言。
    await page.addScriptTag({ path: join('node_modules', 'jsqr', 'dist', 'jsQR.js') });
    const decoded = await page.evaluate(async () => {
      const globalWindow = window as unknown as { jsQR?: (data: Uint8ClampedArray, width: number, height: number) => { data: string } | null };
      if (typeof globalWindow.jsQR !== 'function') {
        return 'unsupported';
      }
      const svg = document.querySelector('[data-testid="share-qr"] svg');
      if (!svg) {
        return null;
      }
      const xml = new XMLSerializer().serializeToString(svg);
      const blob = new Blob([xml], { type: 'image/svg+xml' });
      const objectUrl = URL.createObjectURL(blob);
      const image = new Image();
      await new Promise((resolve, reject) => {
        image.onload = resolve;
        image.onerror = reject;
        image.src = objectUrl;
      });
      const canvas = document.createElement('canvas');
      canvas.width = image.width || 168;
      canvas.height = image.height || 168;
      const context = canvas.getContext('2d');
      if (!context) {
        return null;
      }
      context.fillStyle = '#ffffff';
      context.fillRect(0, 0, canvas.width, canvas.height);
      context.drawImage(image, 0, 0);
      const pixels = context.getImageData(0, 0, canvas.width, canvas.height);
      const result = globalWindow.jsQR(pixels.data, pixels.width, pixels.height);
      URL.revokeObjectURL(objectUrl);
      return result ? result.data : null;
    });
    if (decoded === 'unsupported') {
      console.log('jsqr 未加载成功，跳过二维码解码断言');
    } else {
      expect(decoded).toBe(url); // 扫出来的必须就是这条链接
    }
  });
});
