-- 0007: 回收站、审计日志与异步清理任务
--
-- 设计要点（docs/recovery.md、docs/phases/08-trash-and-restore.md）：
--   * 软删除只是把状态改成 TRASHED 并记 deleted_at，**物理对象不动**，所以恢复是廉价的。
--   * 永久删除先在事务里删数据库记录，再把对象 Key 写进 cleanup_tasks，
--     由后台任务异步删除并重试 —— HTTP 请求绝不长时间阻塞在磁盘操作上。
--   * 对象是否真的能删，由 file_versions 的引用计数决定（阶段六）：
--     清理任务删除前会再查一次，还有人引用就跳过。
--   * 回收站文件仍然占用配额（阶段六的口径），永久删除后才回收。

-- ---- 审计日志 ----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS audit_logs (
    id            UUID PRIMARY KEY,
    actor_id      UUID REFERENCES users (id) ON DELETE SET NULL,
    action        TEXT        NOT NULL,
    resource_type TEXT        NOT NULL,
    resource_id   UUID,
    detail        JSONB       NOT NULL DEFAULT '{}'::jsonb,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT audit_logs_action_check CHECK (length(btrim(action)) > 0),
    CONSTRAINT audit_logs_resource_type_check CHECK (length(btrim(resource_type)) > 0)
);

CREATE INDEX IF NOT EXISTS audit_logs_actor_idx ON audit_logs (actor_id, created_at DESC);
CREATE INDEX IF NOT EXISTS audit_logs_resource_idx
    ON audit_logs (resource_type, resource_id, created_at DESC);
CREATE INDEX IF NOT EXISTS audit_logs_created_idx ON audit_logs (created_at DESC);

-- ---- 异步清理任务 ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS cleanup_tasks (
    id              UUID PRIMARY KEY,
    storage_key     TEXT        NOT NULL,
    reason          TEXT        NOT NULL,
    attempts        INTEGER     NOT NULL DEFAULT 0,
    status          TEXT        NOT NULL,
    last_error      TEXT,
    next_attempt_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT cleanup_tasks_status_check CHECK (status IN ('PENDING', 'DONE', 'FAILED')),
    CONSTRAINT cleanup_tasks_attempts_check CHECK (attempts >= 0),
    CONSTRAINT cleanup_tasks_key_check CHECK (storage_key ~ '^(objects|tmp)/')
);

-- 后台任务只按「待处理 + 到时间」取任务
CREATE INDEX IF NOT EXISTS cleanup_tasks_pending_idx
    ON cleanup_tasks (status, next_attempt_at, created_at);
-- 同一个对象 Key 不需要排很多条任务
CREATE UNIQUE INDEX IF NOT EXISTS cleanup_tasks_pending_key
    ON cleanup_tasks (storage_key) WHERE status = 'PENDING';

-- ---- 回收站扫描索引 ----------------------------------------------------------
CREATE INDEX IF NOT EXISTS files_trashed_idx ON files (deleted_at) WHERE status = 'TRASHED';
CREATE INDEX IF NOT EXISTS folders_trashed_idx ON folders (deleted_at) WHERE status = 'TRASHED';
