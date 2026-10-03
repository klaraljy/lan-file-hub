-- 分享链接加密码（2026-10-03，用户批准的功能之一：对外发文件要能"必须输密码"）
--
-- 与用户口令同口径：**只存 Argon2id 哈希**（PHC 字符串），明文只在创建时由用户自己知道。
-- 空串 = 这条分享不需要密码（默认，行为与之前完全一致）。
ALTER TABLE share_links
  ADD COLUMN IF NOT EXISTS password_hash TEXT NOT NULL DEFAULT '';

COMMENT ON COLUMN share_links.password_hash IS
  '分享密码的 Argon2id 哈希（PHC 字符串）；空串表示该分享不需要密码';
