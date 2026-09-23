-- 在线状态可见性（五阶段改造 ⑤：广场 / 在线用户）
--
-- 用户可以在设置里把自己设成隐身；**默认在线即可见**（用户 2026-09-21 明确要求）。
-- 在线状态本身是易失的（心跳 + 超时），只存在服务端进程内存里、不落库；
-- 这里只持久化"愿不愿意被别人看到"这一个偏好。

ALTER TABLE users ADD COLUMN IF NOT EXISTS presence_visible BOOLEAN NOT NULL DEFAULT TRUE;
