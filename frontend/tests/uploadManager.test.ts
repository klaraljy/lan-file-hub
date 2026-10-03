import { describe, expect, it } from 'vitest';

import { ApiError } from '../src/services/api';
import { MemoryUploadTaskStore } from '../src/services/uploadTaskStore';
import { UploadManager, mapServerStatus, type UploadsApiLike } from '../src/features/uploads/uploadManager';
import type { FileMeta } from '../src/types/file';
import type { CreateUploadResponse, UploadStatusResponse, UploadTask } from '../src/types/upload';

const CHUNK = 4;

function makeFile(name: string, size: number): File {
  const bytes = new Uint8Array(size);
  for (let i = 0; i < size; i += 1) bytes[i] = i % 256;
  return new File([bytes], name);
}

function fileMeta(name: string, size: number): FileMeta {
  return {
    id: `file-${name}`,
    owner_id: 'user-1',
    folder_id: 'folder-1',
    current_version_id: 'version-1',
    name,
    mime_type: 'application/octet-stream',
    size_bytes: size,
    content_hash: 'a'.repeat(64),
    status: 'ACTIVE',
    created_at: '2026-01-01T00:00:00Z',
    updated_at: '2026-01-01T00:00:00Z',
    deleted_at: null,
  };
}

/**
 * 假后端：按真实语义维护会话与已收分块，能验证
 * 「重复分块幂等」「幂等 complete」「中断后按已完成分块续传」。
 */
class FakeApi implements UploadsApiLike {
  readonly chunks = new Map<string, Map<number, number>>();
  readonly putCalls: Array<{ uploadId: string; index: number }> = [];
  readonly cancelled: string[] = [];
  private counter = 0;
  /** 让前 N 次 putChunk 失败，用来验证重试 */
  failNextPuts = 0;
  failWith: () => unknown = () => new ApiError('STORAGE_UNAVAILABLE', '服务器繁忙', '');
  /** 用于模拟「另一个标签页已经完成」 */
  dropChunksFor: string | null = null;
  /** 模拟去重命中：创建会话时服务端就告诉客户端内容已存在 */
  dedupHit = false;
  completedIds = new Set<string>();
  private createCount = 0;

  get createCalls(): number {
    return this.createCount;
  }

  async create(input: {
    folderId: string;
    name: string;
    size: number;
    mimeType?: string;
    sha256?: string;
    chunkSize?: number;
  }): Promise<CreateUploadResponse> {
    this.createCount += 1;
    const uploadId = `upload-${++this.counter}`;
    this.chunks.set(uploadId, new Map());
    const totalChunks = Math.max(1, Math.ceil(input.size / CHUNK));
    if (this.dedupHit) {
      this.dedupHitIds.add(uploadId);
    }
    return {
      upload_id: uploadId,
      chunk_size: CHUNK,
      total_chunks: totalChunks,
      total_size: input.size,
      status: 'INITIATED',
      expires_at: '2026-01-02T00:00:00Z',
      completed_chunks: [],
      already_exists: this.dedupHitIds.has(uploadId),
    };
  }

  async status(uploadId: string): Promise<UploadStatusResponse> {
    const chunks = this.chunks.get(uploadId);
    if (!chunks) {
      throw new ApiError('UPLOAD_NOT_FOUND', '上传会话不存在', '');
    }
    const totalChunks = this.totalChunks.get(uploadId) ?? 1;
    const totalSize = this.totalSize.get(uploadId) ?? 0;
    if (this.completedIds.has(uploadId)) {
      return {
        upload_id: uploadId,
        status: 'COMPLETED',
        file_name: 'x',
        total_size: totalSize,
        chunk_size: CHUNK,
        total_chunks: totalChunks,
        completed_chunks: [...chunks.keys()],
        received_bytes: totalSize,
        expires_at: '2026-01-02T00:00:00Z',
        file_id: 'file-x',
      };
    }
    let received = 0;
    for (const size of chunks.values()) received += size;
    return {
      upload_id: uploadId,
      status: 'UPLOADING',
      file_name: 'x',
      total_size: totalSize,
      chunk_size: CHUNK,
      total_chunks: totalChunks,
      completed_chunks: [...chunks.keys()].sort((a, b) => a - b),
      received_bytes: received,
      expires_at: '2026-01-02T00:00:00Z',
    };
  }

  private readonly totalChunks = new Map<string, number>();
  private readonly totalSize = new Map<string, number>();
  private readonly dedupHitIds = new Set<string>();

  async putChunk(
    uploadId: string,
    index: number,
    data: Blob,
    _chunkSha256: string,
    options: { onProgress?: (loaded: number) => void } = {},
  ): Promise<void> {
    if (this.failNextPuts > 0) {
      this.failNextPuts -= 1;
      throw this.failWith();
    }
    if (this.dropChunksFor === uploadId) {
      throw new ApiError('UPLOAD_ABORTED', '上传已被取消', '');
    }
    this.putCalls.push({ uploadId, index });
    const chunks = this.chunks.get(uploadId);
    if (!chunks) {
      throw new ApiError('UPLOAD_NOT_FOUND', '上传会话不存在', '');
    }
    // 重复分块幂等：已存在就直接成功（与真实后端一致）
    if (!chunks.has(index)) {
      chunks.set(index, data.size);
    }
    options.onProgress?.(data.size);
  }

  async complete(uploadId: string): Promise<FileMeta> {
    const chunks = this.chunks.get(uploadId);
    const expected = this.totalChunks.get(uploadId) ?? 1;
    // 去重命中时后端不需要任何分块就能建出文件（引用已有对象）
    if (!this.dedupHitIds.has(uploadId) && (!chunks || chunks.size < expected)) {
      throw new ApiError('UPLOAD_INCOMPLETE', '分块不完整', '');
    }
    this.completedIds.add(uploadId);
    return fileMeta('uploaded.bin', this.totalSize.get(uploadId) ?? 0);
  }

  async cancel(uploadId: string): Promise<void> {
    this.cancelled.push(uploadId);
    this.chunks.delete(uploadId);
  }

  /** 让会话记住总数，便于 status/complete 判断 */
  remember(uploadId: string, chunkSize: number, totalSize: number): void {
    this.totalChunks.set(uploadId, Math.max(1, Math.ceil(totalSize / chunkSize)));
    this.totalSize.set(uploadId, totalSize);
  }
}

function makeManager(
  api: FakeApi,
  overrides: Partial<{ concurrency: number; maxParallelFiles: number }> = {},
) {
  const store = new MemoryUploadTaskStore();
  const updates: UploadTask[] = [];
  const completed: FileMeta[] = [];
  const manager = new UploadManager({
    api,
    store,
    onTaskChange: (task) => updates.push(task),
    onCompleted: (_task, file) => completed.push(file),
    chunkConcurrency: overrides.concurrency ?? 4,
    maxParallelFiles: overrides.maxParallelFiles,
    // 分块哈希用固定值：这里测的是编排，不是哈希本身
    hashChunk: async () => 'c'.repeat(64),
    hashWholeFile: async () => 'a'.repeat(64),
    // 重试退避不真的等
    sleep: async () => undefined,
  });
  // 假后端需要知道总大小才能判断「分块齐了」
  const originalCreate = api.create.bind(api);
  api.create = async (input) => {
    const response = await originalCreate(input);
    api.remember(response.upload_id, response.chunk_size, input.size);
    return response;
  };
  return { manager, store, updates, completed };
}

describe('mapServerStatus', () => {
  it('把后端状态映射到前端任务状态', () => {
    expect(mapServerStatus('INITIATED')).toBe('initiated');
    expect(mapServerStatus('UPLOADING')).toBe('uploading');
    expect(mapServerStatus('MERGING')).toBe('merging');
    expect(mapServerStatus('VERIFYING')).toBe('verifying');
    expect(mapServerStatus('COMPLETED')).toBe('completed');
    expect(mapServerStatus('FAILED')).toBe('failed');
    expect(mapServerStatus('ABORTED')).toBe('cancelled');
    expect(mapServerStatus('EXPIRED')).toBe('expired');
  });
});

describe('UploadManager.enqueue', () => {
  it('完整上传：切块 → 上传全部分块 → complete', async () => {
    const api = new FakeApi();
    const { manager, store, completed } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('a.bin', 10)], { folderId: 'folder-1' });

    expect(task!.status).toBe('completed');
    expect(task!.percent).toBe(100);
    // 10 字节 / 4 = 3 块
    expect(task!.totalChunks).toBe(3);
    expect(api.putCalls.map((call) => call.index).sort()).toEqual([0, 1, 2]);
    expect(completed).toHaveLength(1);
    // 完成后本地持久化记录要清掉，避免下次刷新又被恢复
    expect(await store.listAll()).toEqual([]);
  });

  it('0 字节文件也能上传（1 个空分块）', async () => {
    const api = new FakeApi();
    const { manager } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('empty.bin', 0)], { folderId: 'folder-1' });

    expect(task!.status).toBe('completed');
    expect(task!.totalChunks).toBe(1);
    expect(api.putCalls.map((call) => call.index)).toEqual([0]);
  });

  it('会话创建后立即持久化，刷新才可能恢复', async () => {
    const api = new FakeApi();
    api.dropChunksFor = 'upload-1';
    const { manager, store } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('b.bin', 10)], { folderId: 'folder-1' });

    expect(task!.status).toBe('failed');
    const persisted = await store.listAll();
    expect(persisted).toHaveLength(1);
    expect(persisted[0]!.folderId).toBe('folder-1');
    expect(persisted[0]!.name).toBe('b.bin');
  });

  it('多文件并发上传，一个失败不影响另一个', async () => {
    const api = new FakeApi();
    const { manager } = makeManager(api);

    const tasks = await manager.enqueue([makeFile('ok1.bin', 5), makeFile('ok2.bin', 9)], {
      folderId: 'folder-1',
    });

    expect(tasks.map((task) => task.status)).toEqual(['completed', 'completed']);
  });
});

describe('UploadManager 去重（阶段六）', () => {
  it('服务端说内容已存在时一个分块都不传，直接完成', async () => {
    const api = new FakeApi();
    api.dedupHit = true;
    const { manager, store, completed } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('dup.bin', 16)], { folderId: 'folder-1' });

    expect(task!.status).toBe('completed');
    expect(task!.percent).toBe(100);
    expect(api.putCalls, '去重命中不应该再传任何分块').toEqual([]);
    expect(completed).toHaveLength(1);
    expect(await store.listAll()).toEqual([]);
  });

  it('未命中去重时仍然正常传分块', async () => {
    const api = new FakeApi();
    api.dedupHit = false;
    const { manager } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('normal.bin', 16)], { folderId: 'folder-1' });

    expect(task!.status).toBe('completed');
    expect(api.putCalls.length, '16 字节 / 4 = 4 块').toBe(4);
  });
});

describe('UploadManager 重试', () => {
  it('可重试错误会自动重试并最终成功', async () => {
    const api = new FakeApi();
    api.failNextPuts = 2; // 前两次失败：一次网络类错误
    const { manager } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('retry.bin', 8)], { folderId: 'folder-1' });

    expect(task!.status).toBe('completed');
    // 失败的分块被重试，最终每个分块都成功过一次
    expect(new Set(api.putCalls.map((call) => call.index)).size).toBe(2);
  });

  it('不可重试错误立即失败，不消耗重试次数', async () => {
    const api = new FakeApi();
    api.failNextPuts = 1;
    api.failWith = () => new ApiError('CHUNK_CONFLICT', '该分块已存在且内容不同', '');
    const { manager } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('conflict.bin', 4)], { folderId: 'folder-1' });

    expect(task!.status).toBe('failed');
    expect(task!.error).toContain('该分块已存在且内容不同');
    // 只尝试了一次
    expect(api.putCalls).toHaveLength(0);
  });

  it('手动重试会跳过后端已完成的分块', async () => {
    const api = new FakeApi();
    api.failNextPuts = 1;
    api.failWith = () => new ApiError('CHUNK_CONFLICT', '冲突', '');
    const { manager } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('resume.bin', 8)], { folderId: 'folder-1' });
    expect(task!.status).toBe('failed');

    // 再传一次：第 0 块已在后端，只补第 1 块
    api.failNextPuts = 0;
    const file = makeFile('resume.bin', 8);
    await manager.retry(task!.id, file);

    const callsAfterRetry = api.putCalls.filter((call) => call.index === 1);
    expect(callsAfterRetry.length).toBeGreaterThan(0);
    expect(manager.get(task!.id)!.status).toBe('completed');
  });

  it('刷新恢复后的任务没有文件对象，重试时会要求重新选择文件', async () => {
    const api = new FakeApi();
    api.failNextPuts = 1;
    api.failWith = () => new ApiError('CHUNK_CONFLICT', '冲突', '');
    const { manager, store } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('need.bin', 4)], { folderId: 'folder-1' });

    // 模拟刷新：新 manager + 同一份持久化存储，内存里的 File 不复存在
    const freshManager = new UploadManager({
      api,
      store,
      onTaskChange: () => undefined,
      hashChunk: async () => 'c'.repeat(64),
      hashWholeFile: async () => 'a'.repeat(64),
      sleep: async () => undefined,
    });
    const restored = await freshManager.restore('folder-1');
    expect(restored).toHaveLength(1);

    await freshManager.retry(restored[0]!.id); // 不传文件

    const updated = freshManager.get(restored[0]!.id)!;
    expect(updated.needsFile).toBe(true);
    expect(updated.status).toBe('paused');
    void task;
  });
});

describe('UploadManager 暂停与取消', () => {
  it('取消会上报后端并标记为已取消', async () => {
    const api = new FakeApi();
    api.failNextPuts = 1;
    api.failWith = () => new ApiError('CHUNK_CONFLICT', '冲突', '');
    const { manager, store } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('cancel.bin', 4)], { folderId: 'folder-1' });
    await manager.cancel(task!.id);

    expect(api.cancelled).toContain(task!.uploadId);
    expect(manager.get(task!.id)!.status).toBe('cancelled');
    expect(await store.listAll()).toEqual([]);
  });

  it('暂停后不再继续传分块', async () => {
    const api = new FakeApi();
    const { manager } = makeManager(api, { concurrency: 1 });
    const file = makeFile('pause.bin', 40);

    // 传第一个分块的过程中暂停
    const pending = manager.enqueue([file], { folderId: 'folder-1' });
    const taskId = manager.list()[0]!.id;
    manager.pause(taskId);
    await pending;

    const task = manager.get(taskId)!;
    expect(['paused', 'cancelled']).toContain(task.status);
    expect(api.putCalls.length).toBeLessThan(10);
  });
});

describe('UploadManager.restore（刷新后恢复）', () => {
  it('用 upload_id 从后端恢复未完成任务，并标出需要重新选文件', async () => {
    const api = new FakeApi();
    // 3 个分块全部失败（不可重试错误），这样后端一个分块都没收到
    api.failNextPuts = 3;
    api.failWith = () => new ApiError('CHUNK_CONFLICT', '冲突', '');
    const first = makeManager(api);

    const [task] = await first.manager.enqueue([makeFile('refresh.bin', 12)], { folderId: 'folder-1' });
    const uploadId = task!.uploadId!;
    // 后端其实已经收到了第 0 块（模拟刷新前传了一半）
    api.chunks.get(uploadId)!.set(0, 4);

    // 模拟刷新：新的 manager，但持久化存储保留（真实刷新时内存状态全部丢失）
    const updates: UploadTask[] = [];
    const restoredManager = new UploadManager({
      api,
      store: first.store,
      onTaskChange: (update) => updates.push(update),
      hashChunk: async () => 'c'.repeat(64),
      hashWholeFile: async () => 'a'.repeat(64),
      sleep: async () => undefined,
    });

    const restored = await restoredManager.restore('folder-1');

    expect(restored).toHaveLength(1);
    expect(restored[0]!.uploadId).toBe(uploadId);
    expect(restored[0]!.status).toBe('paused');
    expect(restored[0]!.needsFile).toBe(true);
    expect(restored[0]!.completedChunks).toEqual([0]);
    expect(restored[0]!.size).toBe(12);
  });

  it('后端已完成或已不存在的会话会被清理，不再恢复', async () => {
    const api = new FakeApi();
    api.failNextPuts = 1;
    api.failWith = () => new ApiError('CHUNK_CONFLICT', '冲突', '');
    const { manager, store } = makeManager(api);

    const [task] = await manager.enqueue([makeFile('gone.bin', 4)], { folderId: 'folder-1' });
    const uploadId = task!.uploadId!;
    api.chunks.delete(uploadId); // 后端已过期清理

    const restored = await manager.restore('folder-1');
    expect(restored).toEqual([]);
    expect(await store.listAll()).toEqual([]);
  });

  it('只恢复当前目录的任务', async () => {
    const api = new FakeApi();
    api.failNextPuts = 2;
    api.failWith = () => new ApiError('CHUNK_CONFLICT', '冲突', '');
    const { manager } = makeManager(api);

    await manager.enqueue([makeFile('one.bin', 4)], { folderId: 'folder-A' });
    await manager.enqueue([makeFile('two.bin', 4)], { folderId: 'folder-B' });

    const restored = await manager.restore('folder-A');
    expect(restored).toHaveLength(1);
    expect(restored[0]!.folderId).toBe('folder-A');
  });
});

/**
 * 2026-10-03 用户实测：一次选一批视频，EP1.mp4 直接「失败 —— 你的并发上传任务已达上限」。
 * 原因是前端把一批文件全部并发丢出去，后端单账号并发会话上限（默认 4）一撞就拒。
 */
describe('UploadManager 文件级排队（避开后端并发上限）', () => {
  it('一批文件同时最多只传 maxParallelFiles 个', async () => {
    const api = new FakeApi();
    const originalPut = api.putChunk.bind(api);
    const live = new Set<string>();
    let maxLive = 0;
    api.putChunk = async (uploadId, index, data, chunkSha256, options) => {
      live.add(uploadId);
      maxLive = Math.max(maxLive, live.size);
      await new Promise((resolve) => setTimeout(resolve, 1));
      try {
        return await originalPut(uploadId, index, data, chunkSha256, options);
      } finally {
        live.delete(uploadId);
      }
    };

    const { manager, completed } = makeManager(api, { maxParallelFiles: 2 });
    const files = Array.from({ length: 6 }, (_, i) => makeFile(`v${i}.bin`, 8));
    const tasks = await manager.enqueue(files, { folderId: 'folder-1' });

    expect(tasks.every((task) => task.status === 'completed')).toBe(true);
    expect(completed).toHaveLength(6);
    // 全部传完，但任意时刻同时在传的文件不超过 2 个
    expect(maxLive).toBeLessThanOrEqual(2);
  });

  it('名额是 manager 级的，分批入队也共用同一个上限', async () => {
    const api = new FakeApi();
    const originalCreate = api.create.bind(api);
    let liveCreates = 0;
    let maxLiveCreates = 0;
    api.create = async (input) => {
      liveCreates += 1;
      maxLiveCreates = Math.max(maxLiveCreates, liveCreates);
      try {
        await new Promise((resolve) => setTimeout(resolve, 1));
        return await originalCreate(input);
      } finally {
        liveCreates -= 1;
      }
    };

    const { manager } = makeManager(api, { maxParallelFiles: 1 });
    // 模拟文件夹上传：按子目录分批 enqueue，两批同时发出去
    await Promise.all([
      manager.enqueue([makeFile('a1.bin', 4), makeFile('a2.bin', 4)], { folderId: 'folder-1' }),
      manager.enqueue([makeFile('b1.bin', 4), makeFile('b2.bin', 4)], { folderId: 'folder-1' }),
    ]);

    expect(maxLiveCreates).toBe(1);
  });

  it('撞上「并发上传任务已达上限」会排队重试，而不是判失败', async () => {
    const api = new FakeApi();
    const { manager } = makeManager(api);
    const innerCreate = api.create.bind(api);
    let busyLeft = 2;
    let attempts = 0;
    api.create = async (input) => {
      attempts += 1;
      if (busyLeft > 0) {
        busyLeft -= 1;
        throw new ApiError('STORAGE_BUSY', '你的并发上传任务已达上限，请先完成或取消其他上传', '');
      }
      return innerCreate(input);
    };

    const [task] = await manager.enqueue([makeFile('ep1.mp4', 8)], { folderId: 'folder-1' });

    expect(task!.status).toBe('completed');
    expect(attempts).toBe(3); // 前两次被拒，第三次成功
  });

  it('排队期间被取消的任务不会白开一个会话', async () => {
    const api = new FakeApi();
    const { manager } = makeManager(api, { maxParallelFiles: 1 });

    const pending = manager.enqueue(
      [makeFile('first.bin', 20), makeFile('second.bin', 4)],
      { folderId: 'folder-1' },
    );
    const [, second] = manager.list();
    await manager.cancel(second!.id);
    await pending;

    // 第二个文件在排队时就被取消，因此只建了第一个文件的会话
    expect(api.createCalls).toBe(1);
    expect(manager.get(second!.id)!.status).toBe('cancelled');
  });
});
