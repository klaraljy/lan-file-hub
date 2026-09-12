-- 0001: 用户表
-- 字段与枚举见 docs/data-model.md §User。
-- 角色：USER | ADMIN；状态：ACTIVE | DISABLED。

CREATE TABLE IF NOT EXISTS users (
    id                  UUID PRIMARY KEY,
    username            TEXT        NOT NULL,
    email               TEXT        NOT NULL,
    password_hash       TEXT        NOT NULL,
    role                TEXT        NOT NULL,
    status              TEXT        NOT NULL,
    storage_quota_bytes BIGINT      NOT NULL,
    used_storage_bytes  BIGINT      NOT NULL DEFAULT 0,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT users_role_check   CHECK (role IN ('USER', 'ADMIN')),
    CONSTRAINT users_status_check CHECK (status IN ('ACTIVE', 'DISABLED')),
    CONSTRAINT users_quota_check  CHECK (storage_quota_bytes >= 0),
    CONSTRAINT users_used_check   CHECK (used_storage_bytes >= 0),
    -- 用户名与邮箱都不允许为空串
    CONSTRAINT users_username_not_blank CHECK (length(btrim(username)) > 0),
    CONSTRAINT users_email_not_blank    CHECK (length(btrim(email)) > 0)
);

-- 用户名与邮箱唯一。大小写不敏感：注册时统一按小写存 username_lower，
-- 这样 "Alice" 与 "alice" 视为同一个用户，符合 docs/data-model.md
-- 「是否大小写敏感必须统一」的要求。
CREATE UNIQUE INDEX IF NOT EXISTS users_username_lower_key ON users (lower(username));
CREATE UNIQUE INDEX IF NOT EXISTS users_email_lower_key    ON users (lower(email));

-- 索引（docs/data-model.md §建议索引）
CREATE INDEX IF NOT EXISTS users_username_idx ON users (username);
CREATE INDEX IF NOT EXISTS users_email_idx    ON users (email);
