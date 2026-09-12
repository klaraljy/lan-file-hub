-- 0005: 文件去重与用户存储配额
--
-- 去重策略（docs/storage.md §去重、docs/data-model.md §存储统计和配额）：
--   * 物理对象按内容寻址，天然去重：相同 SHA-256 只存一份磁盘文件。
--   * 配额按**逻辑大小**计算：用户每引用一次相同内容都算一份，
--     物理去重只用来省磁盘，不用来省配额。
--   * 因此不需要引用计数表：某个对象是否还有人引用，
--     直接由 file_versions 里引用该 content_hash 的行数决定，不会出现计数漂移。

-- 去重预检查：按 (owner_id, content_hash) 查用户自己是否已经有这份内容
CREATE INDEX IF NOT EXISTS files_owner_hash_idx ON files (owner_id, content_hash);

-- 对象引用计数与跨用户物理去重检查：按 content_hash 查所有版本
CREATE INDEX IF NOT EXISTS file_versions_content_hash_idx ON file_versions (content_hash);

-- 配额字段必须是合法非负数（0001 已建列，这里补约束）
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'users_quota_check') THEN
        ALTER TABLE users ADD CONSTRAINT users_quota_check CHECK (storage_quota_bytes >= 0);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'users_used_storage_check') THEN
        ALTER TABLE users
            ADD CONSTRAINT users_used_storage_check CHECK (used_storage_bytes >= 0);
    END IF;
END $$;

-- 一次性校准 used_storage_bytes：
-- 有效口径 = 该用户所有文件版本的大小之和（含回收站与历史版本）。
-- 阶段十一的定期校准任务会重复执行同样的口径。
UPDATE users u
SET used_storage_bytes = COALESCE(
        (
            SELECT SUM(v.size_bytes)
            FROM file_versions v
            JOIN files f ON f.id = v.file_id
            WHERE f.owner_id = u.id
        ),
        0
    ),
    updated_at = now();
