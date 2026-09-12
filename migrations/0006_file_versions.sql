-- 0006: 文件版本与乐观并发控制
--
-- file_versions 在 0003 已经建好（版本号、大小、哈希、对象 Key、创建者、创建时间、is_current），
-- 这里只补「上传新版本」需要的会话字段：
--   * target_file_id     ：本次上传是给已有文件加新版本，而不是新建文件
--                          （注意与 0004 的 file_id 区分：那个是「完成后生成的文件」）
--   * expected_version_id：客户端通过 If-Match 声明的当前版本，用于冲突检测
--
-- 约束（docs/phases/07-versions-and-conflicts.md）：
--   * 不允许直接覆盖当前版本对象：新版本是新的 file_versions 行 + 新的对象 Key
--   * 旧版本必须保留：删除文件前不得删除历史版本行
--   * 版本冲突不能静默覆盖：expected_version_id 与当前版本不一致就报 409

ALTER TABLE upload_sessions
    ADD COLUMN IF NOT EXISTS target_file_id UUID REFERENCES files (id) ON DELETE SET NULL,
    ADD COLUMN IF NOT EXISTS expected_version_id UUID REFERENCES file_versions (id) ON DELETE SET NULL;

-- 每个文件恰好一个当前版本（0003 已有唯一索引，这里补一个查询用的索引）
CREATE INDEX IF NOT EXISTS file_versions_current_idx ON file_versions (file_id) WHERE is_current;

COMMENT ON COLUMN upload_sessions.target_file_id IS
    '非空表示这次上传是给该文件新增版本，完成时不新建文件记录';
COMMENT ON COLUMN upload_sessions.expected_version_id IS
    '客户端 If-Match 声明的当前版本；与 files.current_version_id 不一致时返回 FILE_VERSION_CONFLICT';
