import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { useState } from 'react';

import { folderKeys } from '../folders/useFolders';
import { filesApi, type ListFilesParams } from '../../services/filesApi';
import type { FileMeta } from '../../types/file';

export const fileKeys = {
  all: ['files'] as const,
  list: (folderId: string, params: ListFilesParams) =>
    [...fileKeys.all, 'list', folderId, params] as const,
};

export function useFolderFiles(folderId: string | undefined, params: ListFilesParams = {}) {
  return useQuery({
    queryKey: fileKeys.list(folderId ?? '', params),
    queryFn: () => filesApi.list(folderId!, params),
    enabled: Boolean(folderId),
  });
}

export interface UploadTask {
  id: string;
  name: string;
  size: number;
  percent: number;
  status: 'uploading' | 'success' | 'error';
  error?: string;
}

/**
 * 上传队列。前端状态与 docs/frontend.md 要求一致：
 *   queued / uploading / success / error 必须区分开，
 *   失败要给出可理解的提示，并且**不保留任何文件内容副本**。
 */
export function useUploadQueue(folderId: string | undefined) {
  const queryClient = useQueryClient();
  const [tasks, setTasks] = useState<UploadTask[]>([]);

  function patchTask(id: string, patch: Partial<UploadTask>) {
    setTasks((previous) => previous.map((task) => (task.id === id ? { ...task, ...patch } : task)));
  }

  async function upload(selected: File[]) {
    if (!folderId || selected.length === 0) return;

    // 多文件并发上传，但每个文件单独跟踪进度
    await Promise.all(
      selected.map(async (file) => {
        const id = `${file.name}-${file.size}-${Date.now()}-${Math.random().toString(36).slice(2)}`;
        setTasks((previous) => [
          ...previous,
          { id, name: file.name, size: file.size, percent: 0, status: 'uploading' },
        ]);
        try {
          await filesApi.upload(folderId, file, (percent) => patchTask(id, { percent }));
          patchTask(id, { percent: 100, status: 'success' });
        } catch (error) {
          patchTask(id, {
            status: 'error',
            error: error instanceof Error ? error.message : '上传失败',
          });
        }
      }),
    );

    await queryClient.invalidateQueries({ queryKey: fileKeys.all });
    // 目录里文件数量变了，刷新目录相关查询
    await queryClient.invalidateQueries({ queryKey: folderKeys.all });
  }

  function clearFinished() {
    setTasks((previous) => previous.filter((task) => task.status === 'uploading'));
  }

  return { tasks, upload, clearFinished };
}

export function useDownloadFile() {
  return useMutation({
    mutationFn: (file: FileMeta) => filesApi.download(file),
  });
}

export function fileErrorMessage(error: unknown): string {
  if (error instanceof Error) {
    return error.message;
  }
  return '操作失败，请稍后重试';
}
