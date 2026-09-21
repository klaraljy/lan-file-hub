/**
 * 用户查询接口（「发送给…」选人用）。
 *
 * 契约：`GET /api/v1/users/lookup?q=<关键词>`（需登录）
 *   - `q` 至少 2 个字符，按用户名或邮箱模糊匹配（大小写不敏感），
 *     排除自己、排除非 ACTIVE 用户，最多 10 条；
 *   - 只返回 id 与 username（**不回显邮箱**，但可以用邮箱搜）。
 */
import type { ApiEnvelope } from '../types/auth';
import { http } from './api';

export interface UserLookupItem {
  id: string;
  username: string;
}

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

/** 按用户名或邮箱找可以分享的人；关键词太短时不要调用（后端会回 400）。 */
export async function lookupUsers(q: string): Promise<UserLookupItem[]> {
  const response = await http.get<ApiEnvelope<{ items: UserLookupItem[] }>>('/users/lookup', {
    params: { q },
  });
  return unwrap(response.data).items;
}

export const usersApi = { lookupUsers };
