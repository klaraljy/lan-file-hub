-- 0008: 资源权限与分享链接
--
-- 权限模型（docs/permissions.md）：
--   * 四级：READ < WRITE < OWNER（资源级）+ ADMIN（系统级）
--   * 一条 permissions 记录 = 「某个用户对某个资源拥有某个权限」
--   * 资源的 OWNER 就是 permission='OWNER' 的授权记录，不是单独字段
--   * 继承：文件继承所在文件夹的权限；文件夹继承祖先文件夹的权限；
--     取路径上**最高**的那一级；第一版没有拒绝规则、没有组、没有 ACL 优先级
--
-- 分享链接（docs/permissions.md §5）：
--   * 只保存 Token 的 SHA-256，数据库里没有明文列
--   * 明文 Token 只在创建时返回一次
--   * 过期 / 撤销 / 超出下载次数一律拒绝，不降级放行

CREATE TABLE IF NOT EXISTS permissions (
    id            UUID PRIMARY KEY,
    resource_type TEXT        NOT NULL,
    resource_id   UUID        NOT NULL,
    subject_type  TEXT        NOT NULL,
    subject_id    UUID        NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    permission    TEXT        NOT NULL,
    created_by    UUID        REFERENCES users (id) ON DELETE SET NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT permissions_resource_type_check CHECK (resource_type IN ('file', 'folder')),
    CONSTRAINT permissions_subject_type_check CHECK (subject_type IN ('USER')),
    CONSTRAINT permissions_permission_check CHECK (permission IN ('READ', 'WRITE', 'OWNER'))
);

-- 一个用户对一个资源只能有一条记录：改权限就是更新这一条
CREATE UNIQUE INDEX IF NOT EXISTS permissions_unique_grant
    ON permissions (resource_type, resource_id, subject_type, subject_id);

-- 查某个资源的授权列表（权限管理页）
CREATE INDEX IF NOT EXISTS permissions_resource_idx
    ON permissions (resource_type, resource_id);

-- 查某个用户被授予了哪些资源（「分享给我的」列表与继承查询）
CREATE INDEX IF NOT EXISTS permissions_subject_idx
    ON permissions (subject_type, subject_id);

CREATE TABLE IF NOT EXISTS share_links (
    id             UUID PRIMARY KEY,
    token_hash     TEXT        NOT NULL,  -- SHA-256(明文 Token)，明文不落库
    resource_type  TEXT        NOT NULL,
    resource_id    UUID        NOT NULL,
    permission     TEXT        NOT NULL,
    expires_at     TIMESTAMPTZ,           -- 为空表示不自动过期
    max_downloads  INTEGER,               -- 为空表示不限次数
    download_count INTEGER     NOT NULL DEFAULT 0,
    revoked_at     TIMESTAMPTZ,           -- 非空表示已撤销
    created_by     UUID        REFERENCES users (id) ON DELETE CASCADE,
    last_access_at TIMESTAMPTZ,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT share_links_resource_type_check CHECK (resource_type IN ('file', 'folder')),
    CONSTRAINT share_links_permission_check CHECK (permission IN ('READ', 'WRITE')),
    CONSTRAINT share_links_max_downloads_check CHECK (max_downloads IS NULL OR max_downloads > 0),
    CONSTRAINT share_links_download_count_check CHECK (download_count >= 0),
    CONSTRAINT share_links_token_hash_check CHECK (token_hash ~ '^[0-9a-f]{64}$')
);

-- 公开访问就是按 token_hash 精确查一条
CREATE UNIQUE INDEX IF NOT EXISTS share_links_token_hash_key ON share_links (token_hash);

-- 列出某个资源的分享链接
CREATE INDEX IF NOT EXISTS share_links_resource_idx
    ON share_links (resource_type, resource_id, created_at DESC);

-- 列出某人创建的分享链接（撤销与审计时用）
CREATE INDEX IF NOT EXISTS share_links_creator_idx ON share_links (created_by, created_at DESC);
