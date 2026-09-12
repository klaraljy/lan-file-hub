import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { ApiError } from '../../services/api';
import { foldersApi, type ListChildrenParams } from '../../services/foldersApi';

export const folderKeys = {
  all: ['folders'] as const,
  root: () => [...folderKeys.all, 'root'] as const,
  detail: (id: string) => [...folderKeys.all, 'detail', id] as const,
  path: (id: string) => [...folderKeys.all, 'path', id] as const,
  children: (id: string, params: ListChildrenParams) =>
    [...folderKeys.all, 'children', id, params] as const,
};

export function useRootFolder() {
  return useQuery({
    queryKey: folderKeys.root(),
    queryFn: () => foldersApi.root(),
    staleTime: 5 * 60 * 1000, // 根目录基本不变
  });
}

export function useFolderPath(folderId: string | undefined) {
  return useQuery({
    queryKey: folderKeys.path(folderId ?? ''),
    queryFn: () => foldersApi.path(folderId!),
    enabled: Boolean(folderId),
  });
}

export function useFolderChildren(folderId: string | undefined, params: ListChildrenParams = {}) {
  return useQuery({
    queryKey: folderKeys.children(folderId ?? '', params),
    queryFn: () => foldersApi.children(folderId!, params),
    enabled: Boolean(folderId),
  });
}

/** 变更后统一刷新目录树相关查询 */
function useInvalidateFolders() {
  const queryClient = useQueryClient();
  return () => queryClient.invalidateQueries({ queryKey: folderKeys.all });
}

export function useCreateFolder() {
  const invalidate = useInvalidateFolders();
  return useMutation({
    mutationFn: ({ parentId, name }: { parentId: string; name: string }) =>
      foldersApi.create(parentId, name),
    onSuccess: invalidate,
  });
}

export function useRenameFolder() {
  const invalidate = useInvalidateFolders();
  return useMutation({
    mutationFn: ({ folderId, name }: { folderId: string; name: string }) =>
      foldersApi.rename(folderId, name),
    onSuccess: invalidate,
  });
}

export function useMoveFolder() {
  const invalidate = useInvalidateFolders();
  return useMutation({
    mutationFn: ({ folderId, parentId }: { folderId: string; parentId: string }) =>
      foldersApi.move(folderId, parentId),
    onSuccess: invalidate,
  });
}

export function useDeleteFolder() {
  const invalidate = useInvalidateFolders();
  return useMutation({
    mutationFn: (folderId: string) => foldersApi.remove(folderId),
    onSuccess: invalidate,
  });
}

/** 把任意异常转成可直接展示的文案（后端返回的 message 已经是中文） */
export function folderErrorMessage(error: unknown): string {
  if (error instanceof ApiError) {
    return error.message;
  }
  return '操作失败，请稍后重试';
}
