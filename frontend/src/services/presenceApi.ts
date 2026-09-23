/**
 * 在线状态接口（五阶段改造 ⑤：广场，AGENTS.md §15）。
 *
 * 契约见 `docs/api.md`「在线状态端点」：
 *   POST /presence/heartbeat    心跳（每 30 秒一次），返回在线列表与我的设备
 *   GET  /presence/online       只取在线列表
 *   PUT  /presence/visibility   设置「是否让别人看到我在线」（默认可见）
 *
 * 语义要点：`online` **不含自己**、**不含隐身用户**，同一个人多设备只出现一次；
 * 服务端 90 秒收不到心跳就视为离线（状态只在内存里，重启即清空）。
 */
import type { ApiEnvelope } from '../types/auth';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

/** 别人看到的在线信息（不含 device_id） */
export interface OnlineUser {
  user_id: string;
  username: string;
  device_name: string;
  platform: string;
  /** 最后一次心跳的 Unix 秒 */
  last_seen: number;
}

/** 自己的设备（多一个 device_id，用来判断"哪台是我现在这台"） */
export interface MyDevice extends OnlineUser {
  device_id: string;
  visible: boolean;
}

export interface HeartbeatResult {
  visible: boolean;
  online: OnlineUser[];
  my_devices: MyDevice[];
  ttl_seconds: number;
}

export interface HeartbeatInput {
  device_id: string;
  device_name: string;
  platform: string;
}

export const presenceApi = {
  async heartbeat(input: HeartbeatInput): Promise<HeartbeatResult> {
    const response = await http.post<ApiEnvelope<HeartbeatResult>>('/presence/heartbeat', input);
    return unwrap(response.data);
  },

  async online(): Promise<OnlineUser[]> {
    const response = await http.get<ApiEnvelope<{ items: OnlineUser[] }>>('/presence/online');
    return unwrap(response.data).items;
  },

  /** 返回设置后的值（true = 别人能看到我在线） */
  async setVisible(visible: boolean): Promise<boolean> {
    const response = await http.put<ApiEnvelope<{ visible: boolean }>>('/presence/visibility', {
      visible,
    });
    return unwrap(response.data).visible;
  },
};
