/**
 * 版本历史的查询与操作（阶段七）。
 * 破坏性操作（恢复历史版本）必须二次确认，冲突要给出可解释的提示。
 */
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { ApiError } from '../../services/api';
import { versionsApi } from '../../services/versionsApi';
import type { FileMeta } from '../../types/file';
import type { FileVersion } from '../../types/version';
import { fileKeys } from '../files/useFiles';

export const versionKeys = {
  all: ['versions'] as const,
  list: (fileId: string) => [...versionKeys.all, 'list', fileId] as const,
};

export function useFileVersions(fileId: string | undefined, enabled = true) {
  return useQuery({
    queryKey: versionKeys.list(fileId ?? ''),
    queryFn: () => versionsApi.list(fileId!),
    enabled: Boolean(fileId) && enabled,
  });
}

export function useDownloadVersion() {
  return useMutation({
    mutationFn: ({ file, version }: { file: FileMeta; version: FileVersion }) =>
      versionsApi.download(file, version),
  });
}

export function useRestoreVersion(fileId: string | undefined) {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: ({
      version,
      currentVersionId,
    }: {
      version: FileVersion;
      currentVersionId: string | null;
    }) => versionsApi.restore(fileId!, version.id, currentVersionId),
    onSuccess: async () => {
      // 当前版本变了：文件列表与版本列表都要刷新
      await queryClient.invalidateQueries({ queryKey: fileKeys.all });
      await queryClient.invalidateQueries({ queryKey: versionKeys.all });
    },
  });
}

/** 把版本冲突翻译成用户能理解、能操作的提示 */
export function versionErrorMessage(error: unknown): string {
  if (error instanceof ApiError) {
    if (error.code === 'FILE_VERSION_CONFLICT') {
      return '这个文件在你操作期间被其他人生成了新版本，请刷新后重试';
    }
    if (error.code === 'QUOTA_EXCEEDED') {
      return error.message;
    }
    return error.message;
  }
  return '操作失败，请稍后重试';
}

/** 当前版本 id（后端 details 或本地文件元数据） */
export function conflictCurrentVersion(error: unknown): string | null {
  if (error instanceof ApiError) {
    if (error.details.current_version_id) {
      return error.details.current_version_id;
    }
  }
  return null;
}
