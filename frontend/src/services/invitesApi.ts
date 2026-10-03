/**
 * 邀请接口（用户 2026-10-02：不是所有人都能注册，要通过管理员邀请）。
 *
 * 契约见 `docs/api.md`「邀请端点」：
 *   POST   /admin/invites               建邀请（ADMIN）→ **token 明文只返回这一次**
 *   GET    /admin/invites               列出邀请（ADMIN，不含 token）
 *   DELETE /admin/invites/{id}          撤销（ADMIN，幂等）
 *   GET    /public/invites/{token}      校验（公开，注册页用）
 *   GET    /public/registration-policy  当前是否允许自助注册（公开）
 */
import type { ApiEnvelope } from '../types/auth';
import { http } from './api';

function unwrap<T>(envelope: ApiEnvelope<T>): T {
  return envelope.data;
}

export interface AdminInvite {
  id: string;
  note: string;
  created_by: string;
  created_at: string;
  expires_at: string | null;
  revoked_at: string | null;
  max_uses: number;
  used_count: number;
  remaining_uses: number;
}

export interface PublicInvite {
  note: string;
  expires_at: string | null;
  remaining_uses: number;
}

export interface CreateInviteInput {
  note?: string;
  /** 省略 = 不过期；后端限制 5 分钟 ~ 1 年 */
  expiresInSeconds?: number;
  /** 1~1000，默认 1 */
  maxUses?: number;
}

export const invitesApi = {
  async create(input: CreateInviteInput): Promise<{ invite: AdminInvite; token: string }> {
    const response = await http.post<ApiEnvelope<{ invite: AdminInvite; token: string }>>(
      '/admin/invites',
      {
        note: input.note ?? '',
        ...(input.expiresInSeconds ? { expires_in_seconds: input.expiresInSeconds } : {}),
        ...(input.maxUses ? { max_uses: input.maxUses } : {}),
      },
    );
    return unwrap(response.data);
  },

  async list(): Promise<AdminInvite[]> {
    const response = await http.get<ApiEnvelope<{ items: AdminInvite[] }>>('/admin/invites');
    return unwrap(response.data).items;
  },

  async revoke(inviteId: string): Promise<void> {
    await http.delete(`/admin/invites/${inviteId}`);
  },

  async describe(token: string): Promise<PublicInvite> {
    const response = await http.get<ApiEnvelope<PublicInvite>>(
      `/public/invites/${encodeURIComponent(token)}`,
    );
    return unwrap(response.data);
  },

  async registrationPolicy(): Promise<boolean> {
    const response =
      await http.get<ApiEnvelope<{ self_registration: boolean }>>('/public/registration-policy');
    return unwrap(response.data).self_registration;
  },
};
