-- 0003: 文件与文件版本
-- 字段见 docs/data-model.md §File 与 §FileVersion。
--
-- storage_key 由服务端生成（objects/<sha256 前两位>/<sha256>），
-- 绝不使用用户提供的文件名拼接物理路径（docs/storage.md §路径安全）。

CREATE TABLE IF NOT EXISTS files (
    id                 UUID PRIMARY KEY,
    owner_id           UUID        NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    folder_id          UUID        NOT NULL REFERENCES folders (id) ON DELETE CASCADE,
    current_version_id UUID,       -- 指向 file_versions.id，建版本后再回填
    original_name      TEXT        NOT NULL,
    mime_type          TEXT        NOT NULL DEFAULT 'application/octet-stream',
    size_bytes         BIGINT      NOT NULL,
    content_hash       TEXT        NOT NULL,
    storage_key        TEXT        NOT NULL,
    status             TEXT        NOT NULL,
    created_at         TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at         TIMESTAMPTZ NOT NULL DEFAULT now(),
    deleted_at         TIMESTAMPTZ,

    CONSTRAINT files_status_check CHECK (status IN ('ACTIVE', 'TRASHED', 'PURGED')),
    CONSTRAINT files_size_check CHECK (size_bytes >= 0),
    CONSTRAINT files_name_not_blank CHECK (length(btrim(original_name)) > 0),
    CONSTRAINT files_name_length_check CHECK (length(original_name) <= 255),
    CONSTRAINT files_hash_check CHECK (content_hash ~ '^[0-9a-f]{64}$'),
    CONSTRAINT files_storage_key_check CHECK (storage_key ~ '^objects/[0-9a-f]{2}/[0-9a-f]{64}$')
);

-- 同一目录下不能有两个同名有效文件（不区分大小写）
CREATE UNIQUE INDEX IF NOT EXISTS files_sibling_name_key
    ON files (folder_id, lower(original_name)) WHERE status = 'ACTIVE';

CREATE INDEX IF NOT EXISTS files_owner_idx ON files (owner_id);
CREATE INDEX IF NOT EXISTS files_folder_idx ON files (folder_id);
CREATE INDEX IF NOT EXISTS files_content_hash_idx ON files (content_hash);

CREATE TABLE IF NOT EXISTS file_versions (
    id             UUID PRIMARY KEY,
    file_id        UUID        NOT NULL REFERENCES files (id) ON DELETE CASCADE,
    version_number INTEGER     NOT NULL,
    content_hash   TEXT        NOT NULL,
    size_bytes     BIGINT      NOT NULL,
    storage_key    TEXT        NOT NULL,
    created_by     UUID        NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    is_current     BOOLEAN     NOT NULL DEFAULT false,

    CONSTRAINT file_versions_number_check CHECK (version_number >= 1),
    CONSTRAINT file_versions_size_check CHECK (size_bytes >= 0),
    CONSTRAINT file_versions_hash_check CHECK (content_hash ~ '^[0-9a-f]{64}$')
);

-- 版本号从 1 开始递增，同一文件内唯一
CREATE UNIQUE INDEX IF NOT EXISTS file_versions_file_number_key
    ON file_versions (file_id, version_number);

-- 每个文件最多一个当前版本
CREATE UNIQUE INDEX IF NOT EXISTS file_versions_one_current
    ON file_versions (file_id) WHERE is_current;

CREATE INDEX IF NOT EXISTS file_versions_file_idx ON file_versions (file_id, version_number);

-- current_version_id 指向 file_versions：外键在建表后补，避免循环依赖
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint WHERE conname = 'files_current_version_fk'
    ) THEN
        ALTER TABLE files
            ADD CONSTRAINT files_current_version_fk
            FOREIGN KEY (current_version_id) REFERENCES file_versions (id) ON DELETE SET NULL;
    END IF;
END $$;
