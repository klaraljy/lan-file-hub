/**
 * 窄屏（手机 / 窄窗口）判定（AGENTS.md §14.1：手机上难用 = 产品缺陷）。
 *
 * 为什么不直接在组件里用 AntD 的 `Grid.useBreakpoint()`：
 *   1. 首次测量前，所有断点都是 `undefined`，直接写 `!screens.md` 会在**桌面端先渲染一帧手机布局**
 *      （刷新时界面"闪一下"）；这里把规则统一成"未知时按桌面处理"。
 *   2. 断点阈值只在一处定义，避免 4 个组件各写一遍、判断还不一致。
 *
 * 阈值取 AntD 的 `lg`（992px，与 theme 断点一致）：小于它就进入移动布局——
 * 左侧导航收进抽屉、文件列表改用卡片。这样手机（360–430px）与平板竖屏都不会再挤。
 */
import { Grid } from 'antd';

export function useIsMobile(): boolean {
  const screens = Grid.useBreakpoint();
  // 未测量（undefined）时按桌面处理，只有确定小于 lg 才走移动布局
  return screens.lg === false;
}

/** 触控友好的最小高度（手指点得准），统一从这里取，避免各处写不同的数字 */
export const TOUCH_HEIGHT = 44;
