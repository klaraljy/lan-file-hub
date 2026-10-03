/**
 * jsdom 下渲染 AntD 组件需要的浏览器 API 垫片。
 *
 * jsdom 没有 `matchMedia` / `ResizeObserver`，而 AntD 的响应式与部分组件会用到它们
 * （`FileCardList`、`FileGrid` 里就有 Card / Dropdown / Space）。
 * 组件测试是"真的把按钮渲染出来"再断言 DOM —— 这是在没有浏览器可用的机器上，
 * 唯一能证明"主按钮确实是下载"的办法。
 */
import { vi } from 'vitest';

/**
 * 假装视口宽度（组件测试按**桌面**渲染）。
 * AntD 的 `Grid.useBreakpoint()` 靠 `matchMedia('(min-width: 992px)')` 这类查询判断断点，
 * 一律返回 false 会让 jsdom 里所有组件都以为自己是手机（`useIsMobile` → true），
 * 于是"桌面用描边、手机用实心"这类规则就测不出来了。
 */
const VIEWPORT_WIDTH = 1280;

function matchesQuery(query: string): boolean {
  const min = /min-width:\s*([\d.]+)px/.exec(query);
  const max = /max-width:\s*([\d.]+)px/.exec(query);
  let matched = true;
  if (min) matched = matched && VIEWPORT_WIDTH >= Number.parseFloat(min[1]!);
  if (max) matched = matched && VIEWPORT_WIDTH <= Number.parseFloat(max[1]!);
  return matched;
}

Object.defineProperty(window, 'matchMedia', {
  writable: true,
  value: (query: string) => ({
    matches: matchesQuery(query),
    media: query,
    onchange: null,
    addListener: vi.fn(),
    removeListener: vi.fn(),
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
    dispatchEvent: vi.fn(),
  }),
});

if (!globalThis.ResizeObserver) {
  globalThis.ResizeObserver = class {
    observe() {
      // 测试里不需要真实尺寸
    }
    unobserve() {
      // 同上
    }
    disconnect() {
      // 同上
    }
  } as unknown as typeof ResizeObserver;
}

if (!window.getComputedStyle) {
  // jsdom 有 getComputedStyle，但个别环境下会缺；缺了 AntD 的 ripple 会报错
  Object.defineProperty(window, 'getComputedStyle', {
    value: () => ({ getPropertyValue: () => '' }),
  });
}
