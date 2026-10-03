-- 邀请制注册（2026-10-02，用户明确要求：不是所有人都能注册，要通过管理员邀请）
--
-- 与 share_links 同口径：**库里只存 token 的 SHA-256**，明文只在创建时返回一次。
-- used_count / max_uses 用一条原子 UPDATE 消耗，避免并发把一条邀请用超。
-- 关闭自助注册（ALLOW_SELF_REGISTRATION=false）后，注册必须带一条可用邀请。
CREATE TABLE IF NOT EXISTS invites (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  token_hash  TEXT NOT NULL UNIQUE,
  note        TEXT NOT NULL DEFAULT '',
  created_by  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  expires_at  TIMESTAMPTZ,
  max_uses    INTEGER NOT NULL DEFAULT 1 CHECK (max_uses > 0),
  used_count  INTEGER NOT NULL DEFAULT 0 CHECK (used_count >= 0),
  revoked_at  TIMESTAMPTZ,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_invites_created_by ON invites (created_by);
CREATE INDEX IF NOT EXISTS idx_invites_expires_at ON invites (expires_at);
