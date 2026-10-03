/**
 * 「手机怎么连」卡片（2026-10-03，用户明确要求）。
 *
 * 解决的是用户实测过的困惑：手机连不上，不知道该在手机上打开哪个地址 ——
 * 以前只有托盘图标的提示里写着，网页上看不到。
 *
 * 卡片内容：
 *   - 当前该用的**局域网地址**（`http://<局域网IP>:<端口>/`）+ 二维码，手机扫码直接打开；
 *   - 换网络（校园网 ↔ 手机热点）地址自动更新，不用重启、不用刷新（`useServerLanAddress`）；
 *   - 常见连不上的原因与对应做法（客户端隔离 / 大网段自动发现扫不到）写在这里，不让用户去猜。
 */
import { App as AntApp, Alert, Card, Space, Typography } from 'antd';

import { ShareLinkBox } from './ShareLinkBox';
import { useServerLanAddress } from '../hooks/useServerLanAddress';
import { buildSiteUrl } from '../services/publicUrl';

export function PhoneAccessCard() {
  const { message } = AntApp.useApp();
  // 订阅局域网地址：换网络后卡片自己会更新（地址与二维码一起变）
  useServerLanAddress();

  const { url, warning } = buildSiteUrl();

  async function copy(text: string) {
    try {
      await navigator.clipboard.writeText(text);
      message.success('地址已复制，发到手机上就能打开');
    } catch {
      message.warning('复制失败，请手动选中上面的地址复制');
    }
  }

  return (
    <Card title="手机怎么连" style={{ marginBottom: 16 }}>
      <Space direction="vertical" size={12} style={{ width: '100%' }}>
        <Typography.Paragraph style={{ marginBottom: 0 }}>
          手机连上<strong>和电脑同一个 WiFi（或电脑连你的手机热点）</strong>，然后：
          用手机相机 / 微信「扫一扫」下面的二维码，或者把地址敲进手机浏览器（也可以点「复制链接」发给自己）。
          登录同一个账号，看到的文件跟在电脑上完全一样。
        </Typography.Paragraph>

        <ShareLinkBox url={url} warning={warning} onCopy={(text) => void copy(text)} size={180} />

        <Alert
          type="info"
          showIcon
          message="扫码后打不开？按这两条对号入座"
          description={
            <Space direction="vertical" size={2}>
              <span>
                ① <strong>一直转圈/打不开</strong>：这个 WiFi 可能开了「客户端隔离」（校园网、公司网常见），
                同一网络里的设备互相看不见 —— 让<strong>电脑连手机热点</strong>再扫码。
              </span>
              <span>
                ② <strong>手机 App「找电脑」找不到</strong>：校园网这类大网段（例如 10.x 的 /16），
                手机只会扫自己那一段的 254 个地址，可能和电脑不在同一段 ——
                在 App 里点「<strong>添加地址</strong>」，填上面这个地址（IP 加端口，例如
                <code> 10.200.18.61:9180</code>）即可，直接扫码用浏览器打开也一样能用。
              </span>
              <span>
                ③ 提示里如果没有局域网地址：说明电脑没连 WiFi / 网线，或网络里没有默认路由 ——
                先让电脑连上网（热点也行）。
              </span>
            </Space>
          }
        />
      </Space>
    </Card>
  );
}
