-- 0004: 可恢复分块上传
-- 字段见 docs/data-model.md §UploadSession。
--
-- 关键点：上传会话与分块状态**持久化在 PostgreSQL**，Redis 只做缓存与限流；
-- 服务重启或 Redis 重启后必须能靠这张表恢复断点续传。

CREATE TABLE IF NOT EXISTS upload_sessions (
    id               UUID PRIMARY KEY,
    user_id          UUID        NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    folder_id        UUID        NOT NULL REFERENCES folders (id) ON DELETE CASCADE,
    file_name        TEXT        NOT NULL,
    mime_type        TEXT        NOT NULL DEFAULT 'application/octet-stream',
    total_size       BIGINT      NOT NULL,
    chunk_size       INTEGER     NOT NULL,
    total_chunks     INTEGER     NOT NULL,
    expected_hash    TEXT,       -- 客户端声明的 SHA-256，可为空
    status           TEXT        NOT NULL,
    -- 完成后指向生成的文件，让 complete 可以幂等地重复调用
    file_id          UUID        REFERENCES files (id) ON DELETE SET NULL,
    error_code       TEXT,       -- 失败原因（如 HASH_MISMATCH），便于前端提示
    expires_at       TIMESTAMPTZ NOT NULL,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT upload_sessions_status_check CHECK (
        status IN ('INITIATED', 'UPLOADING', 'MERGING', 'VERIFYING',
                   'COMPLETED', 'FAILED', 'ABORTED', 'EXPIRED')
    ),
    CONSTRAINT upload_sessions_size_check CHECK (total_size >= 0),
    -- 分块大小限制在 1 MiB ~ 64 MiB（docs/storage.md）
    CONSTRAINT upload_sessions_chunk_size_check
        CHECK (chunk_size >= 1048576 AND chunk_size <= 67108864),
    CONSTRAINT upload_sessions_total_chunks_check CHECK (total_chunks >= 0),
    CONSTRAINT upload_sessions_name_check CHECK (length(btrim(file_name)) > 0),
    CONSTRAINT upload_sessions_expected_hash_check
        CHECK (expected_hash IS NULL OR expected_hash ~ '^[0-9a-f]{64}$')
);

CREATE INDEX IF NOT EXISTS upload_sessions_user_status_idx ON upload_sessions (user_id, status);
CREATE INDEX IF NOT EXISTS upload_sessions_expires_idx ON upload_sessions (expires_at);

CREATE TABLE IF NOT EXISTS upload_chunks (
    upload_session_id UUID        NOT NULL REFERENCES upload_sessions (id) ON DELETE CASCADE,
    chunk_index       INTEGER     NOT NULL,
    size_bytes        BIGINT      NOT NULL,
    content_hash      TEXT        NOT NULL,
    storage_key       TEXT        NOT NULL,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now(),

    PRIMARY KEY (upload_session_id, chunk_index),
    CONSTRAINT upload_chunks_index_check CHECK (chunk_index >= 0),
    CONSTRAINT upload_chunks_size_check CHECK (size_bytes >= 0),
    CONSTRAINT upload_chunks_hash_check CHECK (content_hash ~ '^[0-9a-f]{64}$')
);

CREATE INDEX IF NOT EXISTS upload_chunks_session_idx ON upload_chunks (upload_session_id, chunk_index);
