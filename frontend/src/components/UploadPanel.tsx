/**
 * 上传面板（docs/frontend.md §2.3）。
 *
 * 支持：多选、拖拽、单文件与总进度、暂停/继续、取消、失败重试、
 *       刷新后恢复未完成任务（需要用户重新选择同一个文件）。
 *
 * 上传状态与页面状态严格区分，用的是 docs/frontend.md §4.2 的枚举。
 */
import { useRef, useState } from 'react';
import { Alert, Button, Card, Progress, Space, Tag, Tooltip, Typography, Upload } from 'antd';
import {
  CloudUploadOutlined,
  PauseOutlined,
  PlayCircleOutlined,
  RedoOutlined,
  CloseOutlined,
} from '@ant-design/icons';

import { useUploads } from '../features/uploads/useUploads';
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

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

interface Props {
  folderId: string | undefined;
  disabled?: boolean;
}

export function UploadPanel({ folderId, disabled }: Props) {
  const { tasks, upload, pause, cancel, retry, resumeWithFile, forget, clearFinished } =
    useUploads(folderId);
  const [resumeTarget, setResumeTarget] = useState<string | null>(null);
  const resumeInputRef = useRef<HTMLInputElement>(null);

  const active = tasks.filter(
    (task) => task.status === 'uploading' || task.status === 'hashing' || task.status === 'retrying',
  );
  const totalBytes = tasks.reduce((sum, task) => sum + task.size, 0);
  const doneBytes = tasks.reduce(
    (sum, task) => sum + Math.round((task.size * task.percent) / 100),
    0,
  );
  const overallPercent = totalBytes > 0 ? Math.round((doneBytes / totalBytes) * 100) : 0;
  const needsFile = tasks.filter((task) => task.needsFile);

  function pickResumeFile(taskId: string) {
    setResumeTarget(taskId);
    resumeInputRef.current?.click();
  }

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
            <Space style={{ width: '100%', justifyContent: 'space-between' }}>
              <Typography.Text strong>
                上传任务（{active.length} 进行中 / 共 {tasks.length}）
              </Typography.Text>
              <Space>
                <Typography.Text type="secondary">
                  {formatBytes(doneBytes)} / {formatBytes(totalBytes)}
                </Typography.Text>
                <Button size="small" onClick={clearFinished}>
                  清除已结束
                </Button>
              </Space>
            </Space>

            <Progress percent={overallPercent} size="small" />

            {tasks.map((task) => (
              <TaskRow
                key={task.id}
                task={task}
                onPause={() => pause(task.id)}
                onResume={() => void retry(task.id)}
                onPickFile={() => pickResumeFile(task.id)}
                onCancel={() => void cancel(task.id)}
                onRetry={() => void retry(task.id)}
                onForget={() => void forget(task.id)}
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

interface TaskRowProps {
  task: UploadTask;
  onPause: () => void;
  onResume: () => void;
  onPickFile: () => void;
  onCancel: () => void;
  onRetry: () => void;
  onForget: () => void;
}

function TaskRow({ task, onPause, onResume, onPickFile, onCancel, onRetry, onForget }: TaskRowProps) {
  const running = task.status === 'uploading' || task.status === 'hashing' || task.status === 'retrying';
  const done = task.status === 'completed';
  const failed = task.status === 'failed' || task.status === 'expired';

  return (
    <div style={{ marginBottom: 10 }}>
      <Space style={{ width: '100%', justifyContent: 'space-between' }} align="center">
        <Space size={6}>
          <Typography.Text ellipsis style={{ maxWidth: 320 }}>
            {task.name}
          </Typography.Text>
          <Tag color={STATUS_COLORS[task.status]}>{STATUS_LABELS[task.status]}</Tag>
          {task.totalChunks > 0 && (
            <Typography.Text type="secondary" style={{ fontSize: 12 }}>
              {task.completedChunks.length}/{task.totalChunks} 块
            </Typography.Text>
          )}
          {task.attempts > 0 && (
            <Typography.Text type="secondary" style={{ fontSize: 12 }}>
              已重试 {task.attempts} 次
            </Typography.Text>
          )}
        </Space>
        <Space size={4}>
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            {formatBytes(task.size)}
          </Typography.Text>
          {running && (
            <Tooltip title="暂停">
              <Button size="small" type="text" icon={<PauseOutlined />} onClick={onPause} />
            </Tooltip>
          )}
          {task.status === 'paused' && !task.needsFile && (
            <Tooltip title="继续">
              <Button size="small" type="text" icon={<PlayCircleOutlined />} onClick={onResume} />
            </Tooltip>
          )}
          {task.needsFile && (
            <Button size="small" onClick={onPickFile}>
              选择文件继续
            </Button>
          )}
          {failed && !task.needsFile && (
            <Tooltip title="重试">
              <Button size="small" type="text" icon={<RedoOutlined />} onClick={onRetry} />
            </Tooltip>
          )}
          {!done && !running && (
            <Tooltip title="取消上传">
              <Button size="small" type="text" danger icon={<CloseOutlined />} onClick={onCancel} />
            </Tooltip>
          )}
          {done && (
            <Button size="small" type="text" onClick={onForget}>
              移除
            </Button>
          )}
        </Space>
      </Space>
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
