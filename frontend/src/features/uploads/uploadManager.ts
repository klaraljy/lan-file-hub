/**
 * 分块上传的编排逻辑（docs/frontend.md §5）。
 *
 * 与 UI 无关，也不直接依赖 axios：接口通过依赖注入传进来，方便单测。
 *
 * 流程：算整文件 SHA-256 → 创建会话 → 查已完成分块 → 并发补传缺失分块
 *       → complete → 通知上层刷新目录。
 *
 * 关键约束：
 *   - 不把整个文件读进内存（哈希与分块都是流式的）
 *   - 不假设分块按顺序到达，也不假设整批一次成功
 *   - 只对可重试错误自动重试，且有次数上限；其余错误直接反馈
 *   - 刷新后用 upload_id 从后端恢复，前端不自己编造进度
 */
import type { FileMeta } from '../../types/file';
import type {
  CreateUploadResponse,
  PersistedUploadTask,
  UploadStatusResponse,
  UploadTask,
  UploadTaskStatus,
} from '../../types/upload';
import { ApiError } from '../../services/api';
import { hashBlob, hashFile } from '../../services/sha256';
import type { UploadTaskStore } from '../../services/uploadTaskStore';
import {
  MAX_PARALLEL_CHUNKS,
  MAX_PARALLEL_FILES,
  isRetryableError,
  isStorageBusyError,
  missingChunks,
  planChunks,
  retryDelayMs,
  runWithConcurrency,
  slotWaitDelayMs,
  type ChunkSlice,
} from './chunkPlan';

/** uploadManager 需要的最小接口，便于测试注入假实现 */
export interface UploadsApiLike {
  create(input: {
    folderId: string;
    name: string;
    size: number;
    mimeType?: string;
    sha256?: string;
    chunkSize?: number;
  }): Promise<CreateUploadResponse>;
  status(uploadId: string): Promise<UploadStatusResponse>;
  putChunk(
    uploadId: string,
    index: number,
    data: Blob,
    chunkSha256: string,
    options?: { onProgress?: (loaded: number) => void; signal?: AbortSignal },
  ): Promise<void>;
  complete(uploadId: string): Promise<FileMeta>;
  cancel(uploadId: string): Promise<void>;
}

export interface UploadManagerOptions {
  api: UploadsApiLike;
  store: UploadTaskStore;
  /** 任务状态变化回调（每次都传新对象，React 才好比较） */
  onTaskChange: (task: UploadTask) => void;
  onCompleted?: (task: UploadTask, file: FileMeta) => void;
  onError?: (task: UploadTask, error: unknown) => void;
  /** 默认 4（docs/frontend.md §5.2） */
  chunkConcurrency?: number;
  /** 同时在传的文件数上限，默认 3（必须小于后端单账号并发会话上限，见 chunkPlan） */
  maxParallelFiles?: number;
  /** 计算分块哈希的实现，测试可替换 */
  hashChunk?: (blob: Blob) => Promise<string>;
  /** 计算整文件哈希的实现，测试可替换 */
  hashWholeFile?: (
    file: Blob,
    chunkBytes: number,
    onProgress?: (progress: { hashedBytes: number; totalBytes: number }) => void,
    isCancelled?: () => boolean,
  ) => Promise<string>;
  sleep?: (ms: number) => Promise<void>;
  now?: () => number;
}

export interface EnqueueOptions {
  folderId: string;
  /** 已知分块大小时可跳过服务端默认值（一般不用传） */
  chunkSize?: number;
  /** 是否计算整文件 SHA-256（默认计算；关掉则只依赖分块哈希 + 服务端重算） */
  computeWholeFileHash?: boolean;
  /** 显示用的来源前缀（文件夹上传时是相对子目录，例如「原版」），只为让用户在任务列表里分得清 */
  displayPrefix?: string;
}

/**
 * 本地失败记录的 key 前缀（2026-10-03）。
 * 会话都没建起来就失败的任务没有 upload_id，用 `local:<前端任务 id>` 当主键，
 * 这样刷新页面后「哪个文件没传成功」还看得见（用户实测反馈"想看哪个失败了，根本看不了"）。
 */
const LOCAL_RECORD_PREFIX = 'local:';

function localRecordKey(taskId: string): string {
  return LOCAL_RECORD_PREFIX + taskId;
}

const defaultSleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

function errorMessage(error: unknown): string {
  if (error instanceof Error && error.message) {
    return error.message;
  }
  return '上传失败，请稍后重试';
}

/** 后端状态 → 前端任务状态 */
export function mapServerStatus(status: UploadStatusResponse['status']): UploadTaskStatus {
  switch (status) {
    case 'COMPLETED':
      return 'completed';
    case 'FAILED':
      return 'failed';
    case 'ABORTED':
      return 'cancelled';
    case 'EXPIRED':
      return 'expired';
    case 'MERGING':
      return 'merging';
    case 'VERIFYING':
      return 'verifying';
    case 'UPLOADING':
      return 'uploading';
    default:
      return 'initiated';
  }
}

interface RuntimeTask {
  task: UploadTask;
  /** 当前文件对象；刷新恢复后为 undefined，需要用户重新选择 */
  file?: File;
  abort?: AbortController;
  /** 正在传输的分块 → 已发送字节数，用来算更细的进度 */
  inflight: Map<number, number>;
  paused: boolean;
}

export class UploadManager {
  private readonly api: UploadsApiLike;
  private store: UploadTaskStore;
  private readonly onTaskChange: (task: UploadTask) => void;
  private readonly onCompleted?: (task: UploadTask, file: FileMeta) => void;
  private readonly onError?: (task: UploadTask, error: unknown) => void;
  private readonly chunkConcurrency: number;
  private readonly maxParallelFiles: number;
  private readonly hashChunk: (blob: Blob) => Promise<string>;
  private readonly hashWholeFile: NonNullable<UploadManagerOptions['hashWholeFile']>;
  private readonly sleep: (ms: number) => Promise<void>;
  private readonly now: () => number;
  private readonly tasks = new Map<string, RuntimeTask>();
  private sequence = 0;
  /** 正在传的文件数（持有名额的任务数） */
  private activeFiles = 0;
  /** 排队等名额的任务：名额释放时按 FIFO 直接转交 */
  private readonly slotWaiters: Array<() => void> = [];

  constructor(options: UploadManagerOptions) {
    this.api = options.api;
    this.store = options.store;
    this.onTaskChange = options.onTaskChange;
    this.onCompleted = options.onCompleted;
    this.onError = options.onError;
    this.chunkConcurrency = options.chunkConcurrency ?? MAX_PARALLEL_CHUNKS;
    this.maxParallelFiles = Math.max(1, options.maxParallelFiles ?? MAX_PARALLEL_FILES);
    this.hashChunk = options.hashChunk ?? hashBlob;
    this.hashWholeFile = options.hashWholeFile ?? hashFile;
    this.sleep = options.sleep ?? defaultSleep;
    this.now = options.now ?? (() => Date.now());
  }

  /** 当前所有任务（按加入顺序） */
  list(): UploadTask[] {
    return [...this.tasks.values()].map((entry) => entry.task);
  }

  /** IndexedDB 是异步打开的，就绪后注入；打开失败时保持传入的降级实现 */
  setStore(store: UploadTaskStore): void {
    this.store = store;
  }

  get(id: string): UploadTask | undefined {
    return this.tasks.get(id)?.task;
  }

  /**
   * 加入一批文件并立即开始上传。
   * 文件之间并发上传（上限 maxParallelFiles，排队等名额），
   * 文件内部的多个分块也并发（上限 chunkConcurrency）。
   */
  async enqueue(files: File[], options: EnqueueOptions): Promise<UploadTask[]> {
    const created: UploadTask[] = [];
    for (const file of files) {
      const id = `task-${this.now()}-${this.sequence++}`;
      const task: UploadTask = {
        id,
        folderId: options.folderId,
        name: file.name,
        size: file.size,
        chunkSize: options.chunkSize ?? 0,
        totalChunks: 0,
        completedChunks: [],
        status: 'queued',
        percent: 0,
        attempts: 0,
        displayPath: options.displayPrefix ? `${options.displayPrefix}/${file.name}` : undefined,
      };
      this.tasks.set(id, { task, file, inflight: new Map(), paused: false });
      this.emit(id);
      created.push(task);
    }

    // 文件之间并发（受名额限制）；单个文件的失败不影响其他文件
    await Promise.all(
      created.map((task) =>
        this.runQueued(task.id, { computeWholeFileHash: options.computeWholeFileHash !== false }),
      ),
    );
    return created.map((task) => this.tasks.get(task.id)?.task ?? task);
  }

  /** 暂停：中断正在传输的分块，保留后端会话以便续传 */
  pause(id: string): void {
    const entry = this.tasks.get(id);
    if (!entry || entry.paused) return;
    entry.paused = true;
    entry.abort?.abort();
    entry.inflight.clear();
    this.patch(id, { status: 'paused' });
  }

  /** 取消：中断传输并让后端丢弃会话与分块 */
  async cancel(id: string): Promise<void> {
    const entry = this.tasks.get(id);
    if (!entry) return;
    entry.paused = true;
    entry.abort?.abort();
    const uploadId = entry.task.uploadId;
    if (uploadId) {
      try {
        await this.api.cancel(uploadId);
      } catch {
        // 后端找不到会话也算取消成功（可能已过期被清理）
      }
      await this.safeStoreRemove(uploadId);
    }
    this.patch(id, { status: 'cancelled', percent: 0, error: undefined });
  }

  /** 手动重试：重新走一遍剩余分块（后端已完成的分块会被跳过） */
  async retry(id: string, file?: File): Promise<void> {
    const entry = this.tasks.get(id);
    if (!entry) return;
    if (file) {
      entry.file = file;
      entry.task = { ...entry.task, needsFile: false, size: file.size };
    }
    if (!entry.file) {
      this.patch(id, { status: 'paused', needsFile: true, error: '请重新选择同一个文件以继续上传' });
      return;
    }
    entry.paused = false;
    this.patch(id, { status: 'queued', error: undefined, attempts: 0 });
    await this.runQueued(id, { computeWholeFileHash: false });
  }

  /**
   * 刷新页面后恢复：用 upload_id 向后端查询真实状态。
   * 已经完成的会话从本地清理；其余标记为 paused + needsFile，等用户重新选文件。
   * 上次**失败**的记录（含只存在本地的 `local:` 记录）恢复成 failed，刷新不丢。
   */
  async restore(folderId: string): Promise<UploadTask[]> {
    let persisted: PersistedUploadTask[] = [];
    try {
      persisted = await this.store.listByFolder(folderId);
    } catch {
      return [];
    }

    const restored: UploadTask[] = [];
    for (const record of persisted) {
      // 只存在本地的失败记录：连会话都没建起来，直接还原成"失败 + 要重新选文件"
      if (record.uploadId.startsWith(LOCAL_RECORD_PREFIX)) {
        const id = `restored-${record.uploadId}`;
        const task: UploadTask = {
          id,
          folderId: record.folderId,
          name: record.name,
          size: record.size,
          chunkSize: record.chunkSize,
          totalChunks: record.totalChunks,
          completedChunks: [],
          status: 'failed',
          percent: 0,
          attempts: 0,
          error: record.error,
          needsFile: true,
          displayPath: record.displayPath,
        };
        this.tasks.set(id, { task, inflight: new Map(), paused: false });
        this.emit(id);
        restored.push(task);
        continue;
      }

      let status: UploadStatusResponse;
      try {
        status = await this.api.status(record.uploadId);
      } catch (error) {
        if (error instanceof ApiError && (error.code === 'UPLOAD_NOT_FOUND' || error.code === 'UPLOAD_EXPIRED')) {
          // 会话没了：没失败过的记录清掉；**失败过的一定留着** ——
          // 那个文件确实没传上去，用户还得知道（2026-10-03）
          if (record.failed) {
            const id = `restored-${record.uploadId}`;
            const failedTask: UploadTask = {
              id,
              folderId: record.folderId,
              name: record.name,
              size: record.size,
              chunkSize: record.chunkSize,
              totalChunks: record.totalChunks,
              completedChunks: [],
              status: 'failed',
              percent: 0,
              attempts: 0,
              error: record.error,
              needsFile: true,
              displayPath: record.displayPath,
            };
            this.tasks.set(id, { task: failedTask, inflight: new Map(), paused: false });
            this.emit(id);
            restored.push(failedTask);
            continue;
          }
          await this.safeStoreRemove(record.uploadId);
          continue;
        }
        continue;
      }

      if (status.status === 'COMPLETED') {
        await this.safeStoreRemove(record.uploadId);
        continue;
      }

      const id = `restored-${record.uploadId}`;
      const completedChunks = [...status.completed_chunks].sort((a, b) => a - b);
      const task: UploadTask = {
        id,
        uploadId: record.uploadId,
        folderId: record.folderId,
        name: status.file_name || record.name,
        size: status.total_size,
        chunkSize: status.chunk_size,
        totalChunks: status.total_chunks,
        completedChunks,
        // 失败过就还原成失败（带上原因），用户点「重试」能接着传
        status: record.failed ? 'failed' : 'paused',
        percent: percentOf(status.received_bytes, status.total_size),
        attempts: 0,
        error: record.error,
        needsFile: true,
        displayPath: record.displayPath,
      };
      this.tasks.set(id, { task, inflight: new Map(), paused: true });
      this.emit(id);
      restored.push(task);
    }
    return restored;
  }

  /** 从任务列表移除（不影响后端会话） */
  async forget(id: string): Promise<void> {
    const entry = this.tasks.get(id);
    if (!entry) return;
    if (entry.task.uploadId) {
      await this.safeStoreRemove(entry.task.uploadId);
    }
    // 本地失败记录也要一起清掉，否则刷新后它又冒出来
    await this.safeStoreRemove(localRecordKey(id));
    this.tasks.delete(id);
  }

  // ---- 内部实现 ------------------------------------------------------------

  /**
   * 先拿到「同时上传文件数」的名额，再真正开跑。
   *
   * ⛔ 名额是 **manager 级**（不是单次 enqueue 级）：一次选 20 个文件、
   *   或者文件夹上传按子目录分批入队，都共用同一个上限，
   *   否则每批各自并发 3 个，加起来照样把后端单账号的会话上限撞爆。
   *   后端超限时返回 STORAGE_BUSY，用户看到的是「失败」（2026-10-03 用户实测：
   *   一批视频里 EP1.mp4 直接失败，EP2.mp4 却传上去了）。
   */
  private async runQueued(id: string, options: { computeWholeFileHash: boolean }): Promise<void> {
    await this.acquireFileSlot();
    try {
      const entry = this.tasks.get(id);
      // 排队期间被暂停 / 取消的：拿到名额也不开工
      if (!entry || entry.paused || entry.task.status === 'cancelled') {
        return;
      }
      await this.run(id, options);
    } finally {
      this.releaseFileSlot();
    }
  }

  private async acquireFileSlot(): Promise<void> {
    if (this.activeFiles < this.maxParallelFiles) {
      this.activeFiles += 1;
      return;
    }
    await new Promise<void>((resolve) => {
      this.slotWaiters.push(resolve);
    });
  }

  private releaseFileSlot(): void {
    const next = this.slotWaiters.shift();
    if (next) {
      next();  // 名额直接转交给下一个等待者，activeFiles 不变
      return;
    }
    this.activeFiles = Math.max(0, this.activeFiles - 1);
  }

  /**
   * 建上传会话。撞上「服务端并发名额已满」（STORAGE_BUSY）时**排队等**，
   * 而不是把这个文件判失败 —— 同一个账号在两台设备上一起传就会撞上。
   * 等满 SLOT_WAIT_DELAYS_MS 还拿不到名额才把错误抛给用户。
   */
  private async createSessionWithSlotWait(
    id: string,
    input: Parameters<UploadsApiLike['create']>[0],
  ): Promise<CreateUploadResponse> {
    for (let attempt = 0; ; attempt += 1) {
      try {
        return await this.api.create(input);
      } catch (error) {
        const entry = this.tasks.get(id);
        const wait = isStorageBusyError(error as { code?: string }) ? slotWaitDelayMs(attempt + 1) : null;
        if (entry?.paused || wait === null) {
          throw error;
        }
        this.patch(id, { status: 'queued', error: '服务器同时在传的文件已满，正在排队等待…' });
        await this.sleep(wait);
        this.patch(id, { status: 'initiated', error: undefined });
      }
    }
  }

  private async run(id: string, options: { computeWholeFileHash: boolean }): Promise<void> {
    const entry = this.tasks.get(id);
    if (!entry || !entry.file) {
      return;
    }
    const file = entry.file;
    const controller = new AbortController();
    entry.abort = controller;
    entry.paused = false;

    try {
      let session: CreateUploadResponse;

      if (entry.task.uploadId) {
        // 续传：直接用后端已有的会话，前端不重复创建
        const status = await this.api.status(entry.task.uploadId);
        if (status.status === 'COMPLETED') {
          await this.finish(id);
          return;
        }
        session = {
          upload_id: status.upload_id,
          chunk_size: status.chunk_size,
          total_chunks: status.total_chunks,
          total_size: status.total_size,
          status: status.status,
          expires_at: status.expires_at,
          completed_chunks: status.completed_chunks,
        };
      } else {
        let wholeHash: string | undefined;
        if (options.computeWholeFileHash) {
          this.patch(id, { status: 'hashing', percent: 0 });
          wholeHash = await this.hashWholeFile(
            file,
            8 * 1024 * 1024,
            (progress) => {
              const percent = percentOf(progress.hashedBytes, progress.totalBytes);
              // 哈希阶段占用 0–10% 的进度条，避免看起来「卡住」
              this.patch(id, { status: 'hashing', percent: Math.round(percent / 10) });
            },
            () => entry.paused,
          );
        }
        if (entry.paused) {
          this.patch(id, { status: 'paused' });
          return;
        }

        this.patch(id, { status: 'initiated' });
        session = await this.createSessionWithSlotWait(id, {
          folderId: entry.task.folderId,
          name: entry.task.name,
          size: file.size,
          mimeType: file.type || 'application/octet-stream',
          sha256: wholeHash,
          chunkSize: entry.task.chunkSize || undefined,
        });

        entry.task = {
          ...entry.task,
          uploadId: session.upload_id,
          chunkSize: session.chunk_size,
          totalChunks: session.total_chunks,
          size: session.total_size,
        };
        // 会话已创建：先持久化，刷新后才能恢复；上一次的"本地失败记录"同时作废
        await this.safeStorePut({
          uploadId: session.upload_id,
          folderId: entry.task.folderId,
          name: entry.task.name,
          size: session.total_size,
          chunkSize: session.chunk_size,
          totalChunks: session.total_chunks,
          createdAt: this.now(),
          displayPath: entry.task.displayPath,
        });
        await this.safeStoreRemove(localRecordKey(id));
        this.emit(id);
      }

      // 去重命中（阶段六）：服务端说「你已经有同样内容的文件」，
      // 那就一个分块都不用传，直接完成——创建的是指向同一物理对象的新文件记录。
      if (session.already_exists && session.total_size === file.size) {
        this.patch(id, { status: 'verifying', percent: 100 });
        const created = await this.api.complete(session.upload_id);
        await this.safeStoreRemove(session.upload_id);
        await this.finish(id, created);
        return;
      }

      const plan = planChunks(session.total_size, session.chunk_size);
      entry.task = { ...entry.task, completedChunks: [...session.completed_chunks] };
      this.emit(id);

      await this.uploadMissingChunks(id, plan.slice(0, session.total_chunks), session.completed_chunks);
      if (entry.paused) {
        this.patch(id, { status: 'paused' });
        return;
      }

      // complete 前再对一次状态：分块齐了才提交
      const latest = await this.api.status(session.upload_id);
      if (latest.status === 'COMPLETED') {
        await this.finish(id);
        return;
      }
      if (latest.completed_chunks.length < session.total_chunks) {
        const stillMissing = missingChunks(plan, latest.completed_chunks);
        this.patch(id, {
          status: 'failed',
          error: `仍有 ${stillMissing.length} 个分块未上传成功，可点击重试`,
        });
        return;
      }

      this.patch(id, { status: 'merging', percent: 100 });
      const created = await this.api.complete(session.upload_id);
      await this.safeStoreRemove(session.upload_id);
      await this.finish(id, created);
    } catch (error) {
      if (entry.paused) {
        this.patch(id, { status: 'paused' });
        return;
      }
      const message = errorMessage(error);
      this.patch(id, { status: 'failed', error: message });
      // 失败也要落盘（2026-10-03）：刷新后用户还得能看见"哪个文件没传成功、为什么"
      await this.rememberFailure(id, message);
      this.onError?.(this.tasks.get(id)?.task ?? entry.task, error);
    }
  }

  /** 记下失败任务：有 upload_id 就用它当主键，没有就用 `local:<任务 id>` */
  private async rememberFailure(id: string, message: string): Promise<void> {
    const entry = this.tasks.get(id);
    if (!entry) return;
    const task = entry.task;
    await this.safeStorePut({
      uploadId: task.uploadId ?? localRecordKey(id),
      folderId: task.folderId,
      name: task.name,
      size: task.size,
      chunkSize: task.chunkSize,
      totalChunks: task.totalChunks,
      createdAt: this.now(),
      failed: true,
      error: message,
      displayPath: task.displayPath,
    });
  }

  private async uploadMissingChunks(
    id: string,
    plan: ChunkSlice[],
    completedChunks: number[],
  ): Promise<void> {
    const entry = this.tasks.get(id);
    if (!entry || !entry.file) return;
    const file = entry.file;
    const pending = missingChunks(plan, completedChunks);
    if (pending.length === 0) {
      return;
    }
    this.patch(id, { status: 'uploading' });

    const results = await runWithConcurrency(pending, this.chunkConcurrency, async (chunk) => {
      if (entry.paused) return;
      await this.uploadOneChunk(id, file, chunk);
    });

    // 把真实失败原因抛出去：runWithConcurrency 不会因为单个任务失败而中断，
    // 如果这里不抛，用户只会看到「还有分块没传完」，看不到 CHUNK_CONFLICT 这类可操作的信息
    const failure = results.find((result) => !result.ok);
    if (failure && !failure.ok && !entry.paused) {
      throw failure.error;
    }
  }

  private async uploadOneChunk(id: string, file: File, chunk: ChunkSlice): Promise<void> {
    const entry = this.tasks.get(id);
    if (!entry || !entry.task.uploadId) return;
    const uploadId = entry.task.uploadId;
    const blob = file.slice(chunk.start, chunk.end);
    const size = blob.size;

    // 哈希也算在做这件事里，前端不依赖服务端替我们发现本地读盘错误
    const chunkHash = await this.hashChunk(blob);

    let attempt = 0;
    for (;;) {
      if (entry.paused) return;
      entry.inflight.set(chunk.index, 0);
      try {
        await this.api.putChunk(uploadId, chunk.index, blob, chunkHash, {
          signal: entry.abort?.signal,
          onProgress: (loaded) => {
            entry.inflight.set(chunk.index, Math.min(loaded, size));
            this.reportProgress(id);
          },
        });
        entry.inflight.delete(chunk.index);
        const done = new Set(entry.task.completedChunks);
        done.add(chunk.index);
        entry.task = {
          ...entry.task,
          completedChunks: [...done].sort((a, b) => a - b),
          attempts: 0,
        };
        this.reportProgress(id);
        return;
      } catch (error) {
        entry.inflight.delete(chunk.index);
        if (entry.paused) return;
        const retryable = isRetryableError(error as { code?: string; status?: number });
        const delay = retryable ? retryDelayMs(attempt + 1) : null;
        if (delay === null) {
          throw error;
        }
        attempt += 1;
        this.patch(id, { status: 'retrying', attempts: attempt, error: errorMessage(error) });
        await this.sleep(delay);
        this.patch(id, { status: 'uploading' });
      }
    }
  }

  private async finish(id: string, file?: FileMeta): Promise<void> {
    const entry = this.tasks.get(id);
    if (!entry) return;
    // 传完了：本地的"待续传 / 失败"记录一并清掉（调用方通常已经清过 upload_id 那条）
    if (entry.task.uploadId) {
      await this.safeStoreRemove(entry.task.uploadId);
    }
    await this.safeStoreRemove(localRecordKey(id));
    this.patch(id, { status: 'completed', percent: 100, error: undefined });
    if (file) {
      this.onCompleted?.(this.tasks.get(id)?.task ?? entry.task, file);
    }
  }

  /** 进度 = （已确认分块字节 + 正在传输的字节）/ 总大小 */
  private reportProgress(id: string): void {
    const entry = this.tasks.get(id);
    if (!entry) return;
    const chunkSize = entry.task.chunkSize || 1;
    let bytes = 0;
    const total = entry.task.totalChunks;
    for (const index of entry.task.completedChunks) {
      bytes += index === total - 1 ? this.lastChunkSize(entry, chunkSize) : chunkSize;
    }
    for (const loaded of entry.inflight.values()) {
      bytes += loaded;
    }
    this.patch(id, { percent: percentOf(bytes, entry.task.size) });
  }

  private lastChunkSize(entry: RuntimeTask, chunkSize: number): number {
    const remainder = entry.task.size % chunkSize;
    if (entry.task.size === 0) return 0;
    return remainder === 0 ? chunkSize : remainder;
  }

  private patch(id: string, patch: Partial<UploadTask>): void {
    const entry = this.tasks.get(id);
    if (!entry) return;
    entry.task = { ...entry.task, ...patch };
    this.emit(id);
  }

  private emit(id: string): void {
    const entry = this.tasks.get(id);
    if (entry) {
      this.onTaskChange({ ...entry.task });
    }
  }

  private async safeStorePut(task: PersistedUploadTask): Promise<void> {
    try {
      await this.store.put(task);
    } catch {
      // 持久化失败不影响本次上传
    }
  }

  private async safeStoreRemove(uploadId: string): Promise<void> {
    try {
      await this.store.remove(uploadId);
    } catch {
      // 忽略
    }
  }
}

function percentOf(done: number, total: number): number {
  if (total <= 0) return done > 0 ? 100 : 0;
  return Math.max(0, Math.min(100, Math.round((done / total) * 100)));
}
