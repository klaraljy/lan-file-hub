/**
 * 未完成上传任务的持久化（docs/frontend.md §4.3）。
 *
 * 只存任务元数据：upload_id、文件名、大小、分块信息。**不存文件内容**，
 * 也不存任何 Token。刷新页面后用 upload_id 向后端查询真实状态。
 */
import type { PersistedUploadTask } from '../types/upload';

export interface UploadTaskStore {
  put(task: PersistedUploadTask): Promise<void>;
  listByFolder(folderId: string): Promise<PersistedUploadTask[]>;
  listAll(): Promise<PersistedUploadTask[]>;
  remove(uploadId: string): Promise<void>;
  clear(): Promise<void>;
}

const DB_NAME = 'lan-file-hub';
const DB_VERSION = 1;
const STORE_NAME = 'pending-uploads';

function requestToPromise<T>(request: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error('IndexedDB 操作失败'));
  });
}

/** 浏览器实现；无 IndexedDB 时（如 SSR 或隐私模式）静默降级为内存实现 */
export class IndexedDbUploadTaskStore implements UploadTaskStore {
  private dbPromise: Promise<IDBDatabase> | null = null;

  constructor(private readonly factory: IDBFactory | undefined = globalThis.indexedDB) {}

  private open(): Promise<IDBDatabase> {
    if (!this.factory) {
      return Promise.reject(new Error('当前环境不支持 IndexedDB'));
    }
    if (!this.dbPromise) {
      this.dbPromise = new Promise((resolve, reject) => {
        const request = this.factory!.open(DB_NAME, DB_VERSION);
        request.onupgradeneeded = () => {
          const db = request.result;
          if (!db.objectStoreNames.contains(STORE_NAME)) {
            const store = db.createObjectStore(STORE_NAME, { keyPath: 'uploadId' });
            store.createIndex('folderId', 'folderId', { unique: false });
          }
        };
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error ?? new Error('无法打开 IndexedDB'));
      });
    }
    return this.dbPromise;
  }

  private async withStore<T>(
    mode: IDBTransactionMode,
    action: (store: IDBObjectStore) => IDBRequest<T>,
  ): Promise<T> {
    const db = await this.open();
    const transaction = db.transaction(STORE_NAME, mode);
    const result = await requestToPromise(action(transaction.objectStore(STORE_NAME)));
    return result;
  }

  async put(task: PersistedUploadTask): Promise<void> {
    await this.withStore('readwrite', (store) => store.put(task));
  }

  async listByFolder(folderId: string): Promise<PersistedUploadTask[]> {
    const all = await this.listAll();
    return all.filter((task) => task.folderId === folderId);
  }

  async listAll(): Promise<PersistedUploadTask[]> {
    const result = await this.withStore<PersistedUploadTask[]>(
      'readonly',
      (store) => store.getAll() as IDBRequest<PersistedUploadTask[]>,
    );
    return result ?? [];
  }

  async remove(uploadId: string): Promise<void> {
    await this.withStore('readwrite', (store) => store.delete(uploadId));
  }

  async clear(): Promise<void> {
    await this.withStore('readwrite', (store) => store.clear());
  }
}

/** 内存实现：测试与不支持 IndexedDB 的环境使用 */
export class MemoryUploadTaskStore implements UploadTaskStore {
  private readonly tasks = new Map<string, PersistedUploadTask>();

  async put(task: PersistedUploadTask): Promise<void> {
    this.tasks.set(task.uploadId, { ...task });
  }

  async listAll(): Promise<PersistedUploadTask[]> {
    return [...this.tasks.values()].map((task) => ({ ...task }));
  }

  async listByFolder(folderId: string): Promise<PersistedUploadTask[]> {
    return (await this.listAll()).filter((task) => task.folderId === folderId);
  }

  async remove(uploadId: string): Promise<void> {
    this.tasks.delete(uploadId);
  }

  async clear(): Promise<void> {
    this.tasks.clear();
  }
}

/**
 * 打开持久化存储：优先 IndexedDB，失败则退回内存实现。
 * 退回时上传仍可用，只是刷新页面后无法自动恢复任务列表。
 */
export async function createUploadTaskStore(): Promise<UploadTaskStore> {
  const store = new IndexedDbUploadTaskStore();
  try {
    await store.listAll();
    return store;
  } catch {
    return new MemoryUploadTaskStore();
  }
}
