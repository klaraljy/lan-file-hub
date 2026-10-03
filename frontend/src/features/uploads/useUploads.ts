/**
 * 上传面板的 React 绑定：把 UploadManager 的任务流接到组件状态，
 * 并在上传完成后刷新目录查询。
 */
import { useQueryClient } from '@tanstack/react-query';
import { App as AntApp } from 'antd';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import { fileKeys } from '../files/useFiles';
import { folderKeys } from '../folders/useFolders';
import { uploadsApi } from '../../services/uploadsApi';
import { createUploadTaskStore, type UploadTaskStore } from '../../services/uploadTaskStore';
import type { FileMeta } from '../../types/file';
import type { UploadTask } from '../../types/upload';
import { UploadManager } from './uploadManager';
import { isDismissible, uploadErrorMessage } from './taskView';

export function useUploads(folderId: string | undefined) {
  const queryClient = useQueryClient();
  const { message } = AntApp.useApp();
  const [tasks, setTasks] = useState<UploadTask[]>([]);
  const storeRef = useRef<UploadTaskStore | null>(null);
  const [storeReady, setStoreReady] = useState(false);

  const refreshListing = useCallback(async () => {
    await queryClient.invalidateQueries({ queryKey: fileKeys.all });
    await queryClient.invalidateQueries({ queryKey: folderKeys.all });
  }, [queryClient]);

  const manager = useMemo(() => {
    return new UploadManager({
      api: uploadsApi,
      // store 就绪前先用内存实现，避免构造函数里做异步
      store:
        storeRef.current ??
        ({
          put: async () => undefined,
          listAll: async () => [],
          listByFolder: async () => [],
          remove: async () => undefined,
          clear: async () => undefined,
        } satisfies UploadTaskStore),
      onTaskChange: (task) => {
        setTasks((previous) => {
          const index = previous.findIndex((item) => item.id === task.id);
          if (index === -1) {
            return [...previous, task];
          }
          const next = [...previous];
          next[index] = task;
          return next;
        });
      },
      onCompleted: (_task: UploadTask, _file: FileMeta) => {
        void refreshListing();
      },
      /**
       * 失败要**主动弹出来**（2026-10-03 用户实测："刚刚失败了，我想看看哪个失败了，发现根本就看不了"）。
       * 之前失败只在任务列表里有一个小红标，一批几十个文件时根本找不到是哪个。
       */
      onError: (task: UploadTask, error: unknown) => {
        const label = task.displayPath ?? task.name;
        message.error(`「${label}」上传失败：${uploadErrorMessage(error)}`);
      },
    });
    // refreshListing 只依赖 queryClient，message 由 antd 保证稳定
  }, [refreshListing, message]);

  // 持久化存储是异步打开的，就绪后替换到 manager 上
  useEffect(() => {
    let cancelled = false;
    void (async () => {
      const store = await createUploadTaskStore();
      if (cancelled) return;
      storeRef.current = store;
      manager.setStore(store);
      setStoreReady(true);
    })();
    return () => {
      cancelled = true;
    };
  }, [manager]);

  // 进入目录时按 upload_id 从后端恢复未完成任务（docs/frontend.md §4.3）
  useEffect(() => {
    if (!folderId || !storeReady) return;
    let cancelled = false;
    void (async () => {
      const restored = await manager.restore(folderId);
      if (cancelled || restored.length === 0) return;
      setTasks((previous) => {
        const known = new Set(previous.map((task) => task.id));
        return [...previous, ...restored.filter((task) => !known.has(task.id))];
      });
    })();
    return () => {
      cancelled = true;
    };
  }, [folderId, manager, storeReady]);

  /** 选好文件后开始上传 */
  const upload = useCallback(
    async (files: File[]) => {
      if (!folderId || files.length === 0) return;
      await manager.enqueue(files, { folderId });
      await refreshListing();
    },
    [folderId, manager, refreshListing],
  );

  /**
   * 传到**指定目录**（2026-10-03「上传文件夹」用）：
   * 目录结构先由调用方建好（`foldersApi`），这里只负责把一批文件放进那个目录。
   * `displayPrefix` 是源文件夹里的相对子目录，只为让任务列表显示「原版/1.mp4」这种可分辨的名字。
   */
  const uploadTo = useCallback(
    async (files: File[], targetFolderId: string, displayPrefix?: string) => {
      if (!targetFolderId || files.length === 0) return;
      await manager.enqueue(files, { folderId: targetFolderId, displayPrefix });
      await refreshListing();
    },
    [manager, refreshListing],
  );

  const pause = useCallback((id: string) => manager.pause(id), [manager]);
  const cancel = useCallback((id: string) => manager.cancel(id), [manager]);
  const retry = useCallback((id: string) => manager.retry(id), [manager]);
  const forget = useCallback(
    async (id: string) => {
      await manager.forget(id);
      setTasks((previous) => previous.filter((task) => task.id !== id));
    },
    [manager],
  );

  /** 恢复任务需要用户重新提供文件（浏览器不会替我们保留文件内容） */
  const resumeWithFile = useCallback(
    async (id: string, file: File) => {
      await manager.retry(id, file);
      await refreshListing();
    },
    [manager, refreshListing],
  );

  /** 只清「已完成 / 已取消」：**失败和过期的必须留着**，否则用户再也看不到是哪个文件没传成功 */
  const clearFinished = useCallback(() => {
    setTasks((previous) => previous.filter((task) => !isDismissible(task.status)));
  }, []);

  return {
    tasks,
    upload,
    uploadTo,
    pause,
    cancel,
    retry,
    resumeWithFile,
    forget,
    clearFinished,
    storeReady,
  };
}

export { isFinished, isDismissible, uploadErrorMessage } from './taskView';
