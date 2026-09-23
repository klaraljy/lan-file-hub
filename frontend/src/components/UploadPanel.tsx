/**
 * 上传面板（docs/frontend.md §2.3）。
 *
 * 支持：多选、拖拽、单文件与总进度、暂停/继续、取消、失败重试、
 *       刷新后恢复未完成任务（需要用户重新选择同一个文件）。
 *
 * 手机端（AGENTS.md §15 阶段 ②）**不再把上传区摆在首屏**：
 *   - 没有任务时只有一个右下角**悬浮「上传」按钮**（在底部 Tab 栏之上）；
 *   - 有任务时变成一条**悬浮进度条**（文件名 + 百分比 + 取消 + 添加 + 全部），
 *     点「全部」用底部面板列出每个任务的暂停/继续/取消；
 *   - 上传完成会弹一句提示，列表里马上就能看到文件。
 *
 * 上传状态与页面状态严格区分，用的是 docs/frontend.md §4.2 的枚举。
 */
import { useEffect, useRef, useState } from 'react';
import { Alert, App as AntApp, Button, Card, Progress, Space, Tag, Tooltip, Typography, Upload } from 'antd';
import {
  CloudUploadOutlined,
  PauseOutlined,
  PlayCircleOutlined,
  PlusOutlined,
  RedoOutlined,
  CloseOutlined,
} from '@ant-design/icons';

import { TAB_BAR_HEIGHT } from '../app/MobileTabBar';
import { ResponsiveSheet } from './ResponsiveSheet';
import { useUploads } from '../features/uploads/useUploads';
import { useIsMobile } from '../hooks/useIsMobile';
import type { UploadTask, UploadTaskStatus } from '../types/upload';

const STATUS_LABELS: Record<UploadTaskStatus, string> = {
  queued: '排队中',
  hashing: '计算 SHA-256',
  initiated: '已创建会话',
  uploading: '上传中',
  paused: '已暂停',
  retrying: '重试中',
  merging: '服务端合并中',
  verifying: '服务端校验中',
  completed: '已完成',
  failed: '失败',
  cancelled: '已取消',
  expired: '会话已过期',
};

const STATUS_COLORS: Record<UploadTaskStatus, string> = {
  queued: 'default',
  hashing: 'processing',
  initiated: 'processing',
  uploading: 'processing',
  paused: 'warning',
  retrying: 'warning',
  merging: 'processing',
  verifying: 'processing',
  completed: 'success',
  failed: 'error',
  cancelled: 'default',
  expired: 'error',
};

/** 正在跑（含在算哈希与重试）的状态 */
const RUNNING_STATUSES: UploadTaskStatus[] = ['uploading', 'hashing', 'retrying'];

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

function pickResumeFile(setTarget: (id: string) => void, input: HTMLInputElement | null, taskId: string) {
  setTarget(taskId);
  input?.click();
}

interface Props {
  folderId: string | undefined;
  disabled?: boolean;
}

export function UploadPanel({ folderId, disabled }: Props) {
  const controller = useUploads(folderId);
  const isMobile = useIsMobile();

  // 手机：悬浮按钮 + 悬浮进度条（首屏留给文件列表）
  if (isMobile) {
    return <MobileUploadBar folderId={folderId} disabled={disabled} controller={controller} />;
  }
  return <DesktopUploadPanel folderId={folderId} disabled={disabled} controller={controller} />;
}

type UploadsController = ReturnType<typeof useUploads>;

/** 桌面端保持原样：上传区常驻在列表上方 */
function DesktopUploadPanel({
  folderId,
  disabled,
  controller,
}: Props & { controller: UploadsController }) {
  const { tasks, upload, pause, cancel, retry, resumeWithFile, forget, clearFinished } = controller;
  const [resumeTarget, setResumeTarget] = useState<string | null>(null);
  const resumeInputRef = useRef<HTMLInputElement>(null);

  const active = tasks.filter((task) => RUNNING_STATUSES.includes(task.status));
  const totalBytes = tasks.reduce((sum, task) => sum + task.size, 0);
  const doneBytes = tasks.reduce(
    (sum, task) => sum + Math.round((task.size * task.percent) / 100),
    0,
  );
  const overallPercent = totalBytes > 0 ? Math.round((doneBytes / totalBytes) * 100) : 0;
  const needsFile = tasks.filter((task) => task.needsFile);

  return (
    <Card size="small" style={{ marginBottom: 16 }} title="上传">
      <Space direction="vertical" style={{ width: '100%' }} size="middle">
        <Upload.Dragger
          multiple
          showUploadList={false}
          disabled={disabled || !folderId}
          beforeUpload={(file, fileList) => {
            // 只在最后一批里触发一次，避免同一次多选被重复提交
            if (file === fileList[fileList.length - 1]) {
              void upload(fileList as unknown as File[]);
            }
            return false;
          }}
        >
          <p className="ant-upload-drag-icon">
            <CloudUploadOutlined />
          </p>
          <p className="ant-upload-text">点击或拖拽文件到这里上传</p>
          <p className="ant-upload-hint">
            大文件自动分块（默认 8 MiB）并在网络中断后从断点继续；上传前会计算 SHA-256。
          </p>
        </Upload.Dragger>

        {needsFile.length > 0 && (
          <Alert
            type="info"
            showIcon
            message={`有 ${needsFile.length} 个上传任务在刷新前未完成`}
            description="服务器上已上传的分块会保留。重新选择同一个文件即可只补传缺失的分块。"
          />
        )}

        {tasks.length > 0 && (
          <>
            <div
              style={{
                display: 'flex',
                flexWrap: 'wrap',
                gap: 6,
                alignItems: 'center',
                justifyContent: 'space-between',
              }}
            >
              <Typography.Text strong style={{ whiteSpace: 'nowrap' }}>
                上传任务（{active.length} 进行中 / 共 {tasks.length}）
              </Typography.Text>
              <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
                <Typography.Text type="secondary" style={{ whiteSpace: 'nowrap' }}>
                  {formatBytes(doneBytes)} / {formatBytes(totalBytes)}
                </Typography.Text>
                <Button size="small" onClick={clearFinished}>
                  清除已结束
                </Button>
              </div>
            </div>

            <Progress percent={overallPercent} size="small" />

            {tasks.map((task) => (
              <TaskRow
                key={task.id}
                task={task}
                onPause={() => pause(task.id)}
                onResume={() => void retry(task.id)}
                onPickFile={() => pickResumeFile(setResumeTarget, resumeInputRef.current, task.id)}
                onCancel={() => void cancel(task.id)}
                onRetry={() => void retry(task.id)}
                onForget={() => void forget(task.id)}
                mobile={false}
              />
            ))}
          </>
        )}
      </Space>

      {/* 恢复上传需要用户重新提供文件内容：浏览器不会替我们保留 File */}
      <input
        ref={resumeInputRef}
        type="file"
        style={{ display: 'none' }}
        onChange={(event) => {
          const picked = event.target.files?.[0];
          event.target.value = '';
          if (picked && resumeTarget) {
            void resumeWithFile(resumeTarget, picked);
          }
          setResumeTarget(null);
        }}
      />
    </Card>
  );
}

/**
 * 手机端上传：一个悬浮按钮（没任务时）或一条悬浮进度条（有任务时），
 * 外加一个「全部任务」底部面板。首屏因此完全留给文件本身。
 */
function MobileUploadBar({
  folderId,
  disabled,
  controller,
}: Props & { controller: UploadsController }) {
  const { message } = AntApp.useApp();
  const { tasks, upload, pause, cancel, retry, resumeWithFile, forget, clearFinished } = controller;
  const [sheetOpen, setSheetOpen] = useState(false);
  const [resumeTarget, setResumeTarget] = useState<string | null>(null);
  const pickInputRef = useRef<HTMLInputElement>(null);
  const resumeInputRef = useRef<HTMLInputElement>(null);

  const active = tasks.filter((task) => RUNNING_STATUSES.includes(task.status));
  const current = active[0] ?? tasks.find((task) => task.status === 'queued') ?? null;
  const needsFile = tasks.filter((task) => task.needsFile);
  // 需要用户处理的任务（暂停 / 失败 / 需要重新选文件）：这时不能把进度条收起来
  const attention = tasks.filter(
    (task) =>
      task.needsFile ||
      task.status === 'paused' ||
      task.status === 'failed' ||
      task.status === 'expired',
  );
  const busy = active.length > 0 || tasks.some((task) => task.status === 'queued');
  // 全部结束（只剩"已完成/已取消"）时收起成一条细提示，别一直挂着占地方
  const showSummaryOnly = !busy && attention.length === 0 && tasks.length > 0;
  const totalBytes = tasks.reduce((sum, task) => sum + task.size, 0);
  const doneBytes = tasks.reduce(
    (sum, task) => sum + Math.round((task.size * task.percent) / 100),
    0,
  );
  const overallPercent = totalBytes > 0 ? Math.round((doneBytes / totalBytes) * 100) : 0;
  const canUpload = !disabled && Boolean(folderId);

  // 上传完成提示：从"有任务在跑"变成"没有在跑"时提示一次（列表会自动刷新）
  const previousActive = useRef(0);
  useEffect(() => {
    if (previousActive.current > 0 && active.length === 0) {
      const completed = tasks.filter((task) => task.status === 'completed').length;
      if (completed > 0) {
        message.success('上传完成，下面的列表里就能看到');
      }
    }
    previousActive.current = active.length;
    // tasks 只用于读取完成数量，长度变化时上面这个判断就会跑一次
  }, [active.length, tasks, message]);

  const fabBottom = TAB_BAR_HEIGHT + 16;

  return (
    <>
      {/* 选择文件的隐藏 input：AntD Upload 的拖拽区在手机上太占地方，这里用系统文件选择器 */}
      <input
        ref={pickInputRef}
        type="file"
        multiple
        style={{ display: 'none' }}
        onChange={(event) => {
          const picked = Array.from(event.target.files ?? []);
          event.target.value = '';
          if (picked.length > 0) {
            void upload(picked);
          }
        }}
      />
      <input
        ref={resumeInputRef}
        type="file"
        style={{ display: 'none' }}
        onChange={(event) => {
          const picked = event.target.files?.[0];
          event.target.value = '';
          if (picked && resumeTarget) {
            void resumeWithFile(resumeTarget, picked);
          }
          setResumeTarget(null);
        }}
      />

      {tasks.length === 0 ? (
        <button
          type="button"
          onClick={() => pickInputRef.current?.click()}
          disabled={!canUpload}
          style={{
            position: 'fixed',
            right: 16,
            bottom: fabBottom,
            zIndex: 95,
            display: 'flex',
            alignItems: 'center',
            gap: 8,
            height: 52,
            paddingInline: 20,
            borderRadius: 26,
            border: 'none',
            background: canUpload ? '#1677ff' : '#d9d9d9',
            color: '#fff',
            fontSize: 16,
            fontWeight: 500,
            boxShadow: '0 6px 16px rgba(22, 119, 255, 0.35)',
          }}
        >
          <CloudUploadOutlined style={{ fontSize: 20 }} />
          上传
        </button>
      ) : showSummaryOnly ? (
        // 全部结束：收起成一条细提示，避免一直占着屏幕（还能点开看历史、一键清掉）
        <div
          style={{
            position: 'fixed',
            left: 12,
            right: 12,
            bottom: fabBottom,
            zIndex: 90,
            display: 'flex',
            alignItems: 'center',
            gap: 8,
            background: '#fff',
            borderRadius: 10,
            boxShadow: '0 4px 14px rgba(0, 0, 0, 0.12)',
            padding: '8px 12px',
          }}
        >
          <Typography.Text type="secondary" style={{ flex: 1, minWidth: 0 }} ellipsis>
            上传完成 {tasks.filter((task) => task.status === 'completed').length} 个
          </Typography.Text>
          <Button size="small" onClick={() => pickInputRef.current?.click()} disabled={!canUpload}>
            继续上传
          </Button>
          <Button size="small" type="link" onClick={() => setSheetOpen(true)}>
            查看
          </Button>
          <Button
            size="small"
            type="text"
            aria-label="收起上传提示"
            icon={<CloseOutlined />}
            onClick={clearFinished}
          />
        </div>
      ) : (
        <div
          style={{
            position: 'fixed',
            left: 12,
            right: 12,
            bottom: fabBottom,
            zIndex: 90,
            background: '#fff',
            borderRadius: 12,
            boxShadow: '0 6px 20px rgba(0, 0, 0, 0.16)',
            padding: 12,
          }}
        >
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <Typography.Text strong ellipsis style={{ flex: 1, minWidth: 0 }}>
              {current ? current.name : `上传任务（${tasks.length}）`}
            </Typography.Text>
            <Button
              size="small"
              aria-label="继续添加文件"
              icon={<PlusOutlined />}
              onClick={() => pickInputRef.current?.click()}
              disabled={!canUpload}
            />
            {current && (
              <Button size="small" onClick={() => void cancel(current.id)}>
                取消
              </Button>
            )}
            <Button size="small" type="link" onClick={() => setSheetOpen(true)}>
              全部（{tasks.length}）
            </Button>
          </div>
          <Progress
            percent={overallPercent}
            size="small"
            status={current ? 'active' : 'normal'}
            style={{ marginBottom: 0 }}
          />
          {attention.length > 0 && (
            <Typography.Text type="warning" style={{ fontSize: 12 }}>
              有 {attention.length} 个任务需要处理（暂停 / 失败 / 需要重新选择文件），点「全部」查看
            </Typography.Text>
          )}
        </div>
      )}

      {/* 全部任务：底部面板（阶段 ②"弹窗在手机上改成底部滑出"） */}
      <ResponsiveSheet
        open={sheetOpen}
        title="上传任务"
        mobileHeight="80%"
        onClose={() => setSheetOpen(false)}
      >
        {needsFile.length > 0 && (
          <Alert
            type="info"
            showIcon
            style={{ marginBottom: 12 }}
            message={`有 ${needsFile.length} 个上传任务在刷新前未完成`}
            description="选择同一个文件即可只补传缺失的分块。"
          />
        )}
        <div
          style={{
            display: 'flex',
            justifyContent: 'space-between',
            alignItems: 'center',
            marginBottom: 8,
          }}
        >
          <Typography.Text type="secondary">
            {active.length} 进行中 / 共 {tasks.length}
          </Typography.Text>
          <Button size="small" onClick={clearFinished}>
            清除已结束
          </Button>
        </div>
        {tasks.map((task) => (
          <TaskRow
            key={task.id}
            task={task}
            onPause={() => pause(task.id)}
            onResume={() => void retry(task.id)}
            onPickFile={() => pickResumeFile(setResumeTarget, resumeInputRef.current, task.id)}
            onCancel={() => void cancel(task.id)}
            onRetry={() => void retry(task.id)}
            onForget={() => void forget(task.id)}
            mobile
          />
        ))}
      </ResponsiveSheet>
    </>
  );
}

interface TaskRowProps {
  task: UploadTask;
  onPause: () => void;
  onResume: () => void;
  onPickFile: () => void;
  onCancel: () => void;
  onRetry: () => void;
  onForget: () => void;
  /** 手机：按钮放大到 44px，手指点得准 */
  mobile: boolean;
}

function TaskRow({
  task,
  onPause,
  onResume,
  onPickFile,
  onCancel,
  onRetry,
  onForget,
  mobile,
}: TaskRowProps) {
  const running = RUNNING_STATUSES.includes(task.status);
  const done = task.status === 'completed';
  const failed = task.status === 'failed' || task.status === 'expired';
  const rowButtonProps = mobile ? { size: 'large' as const } : { size: 'small' as const };

  return (
    <div style={{ marginBottom: 10 }}>
      <div
        style={{
          display: 'flex',
          flexWrap: 'wrap',
          gap: 6,
          alignItems: 'center',
          justifyContent: 'space-between',
        }}
      >
        <div
          style={{
            display: 'flex',
            flexWrap: 'wrap',
            gap: 6,
            alignItems: 'center',
            minWidth: 0,
            flex: '1 1 180px',
          }}
        >
          <Typography.Text ellipsis style={{ maxWidth: mobile ? '100%' : 320, minWidth: 0 }}>
            {task.name}
          </Typography.Text>
          <Tag color={STATUS_COLORS[task.status]} style={{ whiteSpace: 'nowrap' }}>
            {STATUS_LABELS[task.status]}
          </Tag>
          {task.totalChunks > 0 && (
            <Typography.Text type="secondary" style={{ fontSize: 12, whiteSpace: 'nowrap' }}>
              {task.completedChunks.length}/{task.totalChunks} 块
            </Typography.Text>
          )}
          {task.attempts > 0 && (
            <Typography.Text type="secondary" style={{ fontSize: 12, whiteSpace: 'nowrap' }}>
              已重试 {task.attempts} 次
            </Typography.Text>
          )}
        </div>
        <div style={{ display: 'flex', gap: 4, alignItems: 'center' }}>
          <Typography.Text type="secondary" style={{ fontSize: 12, whiteSpace: 'nowrap' }}>
            {formatBytes(task.size)}
          </Typography.Text>
          {running && (
            <Tooltip title="暂停">
              <Button {...rowButtonProps} type="text" icon={<PauseOutlined />} onClick={onPause} />
            </Tooltip>
          )}
          {task.status === 'paused' && !task.needsFile && (
            <Tooltip title="继续">
              <Button
                {...rowButtonProps}
                type="text"
                icon={<PlayCircleOutlined />}
                onClick={onResume}
              />
            </Tooltip>
          )}
          {task.needsFile && (
            <Button {...rowButtonProps} onClick={onPickFile}>
              选择文件继续
            </Button>
          )}
          {failed && !task.needsFile && (
            <Tooltip title="重试">
              <Button {...rowButtonProps} type="text" icon={<RedoOutlined />} onClick={onRetry} />
            </Tooltip>
          )}
          {!done && !running && (
            <Tooltip title="取消上传">
              <Button
                {...rowButtonProps}
                type="text"
                danger
                icon={<CloseOutlined />}
                onClick={onCancel}
              />
            </Tooltip>
          )}
          {done && (
            <Button {...rowButtonProps} type="text" onClick={onForget}>
              移除
            </Button>
          )}
        </div>
      </div>
      <Progress
        percent={task.percent}
        size="small"
        status={failed ? 'exception' : done ? 'success' : running ? 'active' : 'normal'}
      />
      {task.error && (
        <Typography.Text type="danger" style={{ fontSize: 12 }}>
          {task.error}
        </Typography.Text>
      )}
    </div>
  );
}
