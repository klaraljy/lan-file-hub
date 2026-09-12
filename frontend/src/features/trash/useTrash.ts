/**
 * 回收站的数据绑定（阶段八）。
 * 恢复与永久删除都是破坏性/重要操作，所以在 UI 层必须二次确认；
 * 永久删除是异步的（对象由后台任务删），提示里要说清楚。
 */
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { ApiError } from '../../services/api';
import { trashApi, type ListTrashParams } from '../../services/trashApi';
import { fileKeys } from '../files/useFiles';
import { folderKeys } from '../folders/useFolders';

export const trashKeys = {
  all: ['trash'] as const,
  list: (params: ListTrashParams) => [...trashKeys.all, 'list', params] as const,
};

export function useTrash(params: ListTrashParams = {}) {
  return useQuery({
    queryKey: trashKeys.list(params),
    queryFn: () => trashApi.list(params),
  });
}

export function useRestoreFromTrash() {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: (resourceId: string) => trashApi.restore(resourceId),
    onSuccess: async () => {
      // 恢复会同时影响回收站与目录列表
      await queryClient.invalidateQueries({ queryKey: trashKeys.all });
      await queryClient.invalidateQueries({ queryKey: fileKeys.all });
      await queryClient.invalidateQueries({ queryKey: folderKeys.all });
    },
  });
}

export function usePurgeFromTrash() {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: (resourceId: string) => trashApi.purge(resourceId),
    onSuccess: async () => {
      await queryClient.invalidateQueries({ queryKey: trashKeys.all });
      await queryClient.invalidateQueries({ queryKey: fileKeys.all });
      await queryClient.invalidateQueries({ queryKey: folderKeys.all });
    },
  });
}

/** 把回收站相关错误翻译成用户能理解、能照做的提示 */
export function trashErrorMessage(error: unknown): string {
  if (error instanceof ApiError) {
    if (error.code === 'RESOURCE_CONFLICT') {
      const name = error.details.conflict_name;
      return name
        ? `目标目录下已经有同名资源「${name}」，请先改名或删掉它再恢复`
        : '目标目录下已有同名资源，请先处理后再恢复';
    }
    if (error.code === 'QUOTA_EXCEEDED') {
      return error.message;
    }
    if (error.code === 'FORBIDDEN') {
      return '这个资源不能执行该操作';
    }
    return error.message;
  }
  return '操作失败，请稍后重试';
}
