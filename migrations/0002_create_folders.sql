-- 0002: 文件夹表
-- 字段见 docs/data-model.md §Folder。
--
-- path_key 是物化路径，格式为 "/<祖先id>/<...>/<自身id>/"：
--   - 根目录：            /<root_id>/
--   - 根下的子目录：      /<root_id>/<child_id>/
--   - 孙目录：            /<root_id>/<child_id>/<grandchild_id>/
-- 有了它，「目标是否在自己的子树里」与「整棵子树改名/软删除」都是一条 SQL。

CREATE TABLE IF NOT EXISTS folders (
    id          UUID PRIMARY KEY,
    owner_id    UUID        NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    parent_id   UUID        REFERENCES folders (id) ON DELETE CASCADE,
    name        TEXT        NOT NULL,
    path_key    TEXT        NOT NULL,
    status      TEXT        NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    deleted_at  TIMESTAMPTZ,

    CONSTRAINT folders_status_check CHECK (status IN ('ACTIVE', 'TRASHED', 'PURGED')),
    CONSTRAINT folders_name_length_check CHECK (length(name) <= 255),
    CONSTRAINT folders_path_key_check CHECK (path_key ~ '^/([0-9a-f-]{36}/)+$'),
    -- 根目录没有名字、也没有父目录
    CONSTRAINT folders_root_shape_check CHECK (
        (parent_id IS NULL AND name = '') OR (parent_id IS NOT NULL AND length(name) > 0)
    )
);

-- 每个用户恰好一个根目录
CREATE UNIQUE INDEX IF NOT EXISTS folders_one_root_per_user
    ON folders (owner_id) WHERE parent_id IS NULL;

-- 同一父目录下不能有同名有效文件夹；名称比较不区分大小写
CREATE UNIQUE INDEX IF NOT EXISTS folders_sibling_name_key
    ON folders (parent_id, lower(name)) WHERE status = 'ACTIVE' AND parent_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS folders_owner_idx ON folders (owner_id);
CREATE INDEX IF NOT EXISTS folders_parent_idx ON folders (parent_id);
-- text_pattern_ops 让 path_key LIKE 'prefix%' 走索引
CREATE INDEX IF NOT EXISTS folders_path_key_idx ON folders (path_key text_pattern_ops);

-- 给已存在的用户补建根目录（迁移必须幂等，可重复执行）
WITH missing AS (
    SELECT u.id AS owner_id, gen_random_uuid() AS folder_id
    FROM users u
    WHERE NOT EXISTS (
        SELECT 1 FROM folders f WHERE f.owner_id = u.id AND f.parent_id IS NULL
    )
)
INSERT INTO folders (id, owner_id, parent_id, name, path_key, status)
SELECT folder_id, owner_id, NULL, '', '/' || folder_id::text || '/', 'ACTIVE'
FROM missing;
