/**
 * 权限与分享的数据绑定（阶段九）。
 * 所有判定以后端为准：前端只用有效权限决定「显示哪些按钮」，不作为安全依据。
 */
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { ApiError } from '../../services/api';
import { sharingApi, type CreateShareInput } from '../../services/sharingApi';
import type { PermissionValue, SharePermission } from '../../types/share';

export const sharingKeys = {
  all: ['sharing'] as const,
  permissions: (resourceId: string) => [...sharingKeys.all, 'permissions', resourceId] as const,
  effective: (resourceId: string) => [...sharingKeys.all, 'effective', resourceId] as const,
  shares: (resourceId: string) => [...sharingKeys.all, 'shares', resourceId] as const,
  sharedWithMe: () => [...sharingKeys.all, 'shared-with-me'] as const,
};

/** 「分享给我的」：别人授权给我的文件与文件夹 */
export function useSharedWithMe() {
  return useQuery({
    queryKey: sharingKeys.sharedWithMe(),
    queryFn: () => sharingApi.listSharedWithMe(),
  });
}

export function useEffectiveLevel(resourceId: string | undefined, enabled = true) {
  return useQuery({
    queryKey: sharingKeys.effective(resourceId ?? ''),
    queryFn: () => sharingApi.effectiveLevel(resourceId!),
    enabled: Boolean(resourceId) && enabled,
  });
}

export function usePermissions(resourceId: string | undefined, enabled = true) {
  return useQuery({
    queryKey: sharingKeys.permissions(resourceId ?? ''),
    queryFn: () => sharingApi.listPermissions(resourceId!),
    enabled: Boolean(resourceId) && enabled,
  });
}

export function useGrantPermission(resourceId: string | undefined) {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: ({ userId, permission }: { userId: string; permission: PermissionValue }) =>
      sharingApi.grant(resourceId!, userId, permission),
    onSuccess: async () => {
      await queryClient.invalidateQueries({ queryKey: sharingKeys.permissions(resourceId ?? '') });
    },
  });
}

export function useRevokePermission(resourceId: string | undefined) {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: (userId: string) => sharingApi.revoke(resourceId!, userId),
    onSuccess: async () => {
      await queryClient.invalidateQueries({ queryKey: sharingKeys.permissions(resourceId ?? '') });
    },
  });
}

export function useShares(resourceId: string | undefined, enabled = true) {
  return useQuery({
    queryKey: sharingKeys.shares(resourceId ?? ''),
    queryFn: () => sharingApi.listShares(resourceId!),
    enabled: Boolean(resourceId) && enabled,
  });
}

export function useCreateShare(resourceId: string | undefined) {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: (input: CreateShareInput) => sharingApi.createShare(resourceId!, input),
    onSuccess: async () => {
      await queryClient.invalidateQueries({ queryKey: sharingKeys.shares(resourceId ?? '') });
    },
  });
}

export function useRevokeShare(resourceId: string | undefined) {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: (shareId: string) => sharingApi.revokeShare(shareId),
    onSuccess: async () => {
      await queryClient.invalidateQueries({ queryKey: sharingKeys.shares(resourceId ?? '') });
    },
  });
}

/** 权限等级的中文说明，前端与提示共用 */
export const PERMISSION_LABELS: Record<PermissionValue, string> = {
  READ: '只读（可查看与下载）',
  WRITE: '可写（可上传、修改、删除）',
  OWNER: '所有者（可管理权限与分享）',
};

export const SHARE_PERMISSION_LABELS: Record<SharePermission, string> = {
  READ: '只读',
  WRITE: '可写',
};

export function sharingErrorMessage(error: unknown): string {
  if (error instanceof ApiError) {
    if (error.code === 'RESOURCE_NOT_FOUND') {
      return '资源不存在，或你没有权限管理它';
    }
    if (error.code === 'FORBIDDEN') {
      return error.message || '你没有执行该操作的权限';
    }
    if (error.code === 'RESOURCE_CONFLICT') {
      return error.message;
    }
    return error.message;
  }
  return '操作失败，请稍后重试';
}
