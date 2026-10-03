/**
 * 「发文件给某人」选择器（2026-10-03）。
 *
 * 用户实测的抱怨：「点击发文件，怎么不能按文件夹的形式发送，然后得到文件再在里面下载呢」。
 * 原来的实现只列**根目录下的文件**：选不了文件夹，也进不去子目录。
 *
 * 现在像一个迷你资源管理器：上面是面包屑（可以一层层进去），下面是当前目录的
 * **文件夹 + 文件**；点文件夹名进去，点右边的按钮把这一项发出去（文件夹整个发）。
 * 对方在「分享给我的」里点开文件夹就能一层层看并下载 —— 后端权限本来就沿目录继承
 * （`permission_checker.cpp` 的 `EffectiveLevel`），不用改后端。
 */
import { useEffect, useMemo, useState } from 'react';
import { Alert, App as AntApp, Button, Empty, Input, List, Space, Spin, Tag, Typography } from 'antd';
import { FileOutlined, FolderOpenOutlined, SendOutlined } from '@ant-design/icons';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { FolderBreadcrumb } from './FolderBreadcrumb';
import { ResponsiveSheet } from './ResponsiveSheet';
import { folderKeys } from '../features/folders/useFolders';
import { fileKeys } from '../features/files/useFiles';
import { sharingKeys } from '../features/sharing/useSharing';
import {
  filterPickerRows,
  mergePickerRows,
  pickerLocationLabel,
  sentMessage,
} from '../features/sharing/sendPicker';
import { filesApi } from '../services/filesApi';
import { foldersApi, type ListChildrenParams } from '../services/foldersApi';
import { sharingApi } from '../services/sharingApi';
import type { OnlineUser } from '../services/presenceApi';

/** 一层里最多列这么多（弹窗里不做分页，超出提示一下） */
const PICKER_PAGE_SIZE = 100;

const PICKER_PARAMS: ListChildrenParams = {
  page_size: PICKER_PAGE_SIZE,
  sort_by: 'name',
  sort_order: 'asc',
};

interface Props {
  /** 要发给谁；为 null 时不显示 */
  target: OnlineUser | null;
  onClose: () => void;
}

export function SendFilePicker({ target, onClose }: Props) {
  const { message } = AntApp.useApp();
  const queryClient = useQueryClient();
  const [folderId, setFolderId] = useState<string | undefined>(undefined);
  const [keyword, setKeyword] = useState('');
  const [sendingId, setSendingId] = useState<string | null>(null);

  const open = Boolean(target);
  const root = useQuery({
    queryKey: folderKeys.root(),
    queryFn: () => foldersApi.root(),
    enabled: open,
  });
  const currentId = folderId ?? root.data?.id;

  // 每次打开都从根目录、空关键字开始（上次翻到哪不该影响这一次）
  useEffect(() => {
    if (open) {
      setFolderId(undefined);
      setKeyword('');
    }
  }, [open, target?.user_id]);

  const path = useQuery({
    queryKey: folderKeys.path(currentId ?? ''),
    queryFn: () => foldersApi.path(currentId!),
    enabled: open && Boolean(currentId),
  });
  const folders = useQuery({
    queryKey: folderKeys.children(currentId ?? '', PICKER_PARAMS),
    queryFn: () => foldersApi.children(currentId!, PICKER_PARAMS),
    enabled: open && Boolean(currentId),
  });
  const files = useQuery({
    queryKey: fileKeys.list(currentId ?? '', PICKER_PARAMS),
    queryFn: () => filesApi.list(currentId!, PICKER_PARAMS),
    enabled: open && Boolean(currentId),
  });

  const grant = useMutation({
    mutationFn: ({ resourceId, userId }: { resourceId: string; userId: string }) =>
      sharingApi.grant(resourceId, userId, 'READ'),
    onSuccess: async (_data, variables) => {
      await queryClient.invalidateQueries({
        queryKey: sharingKeys.permissions(variables.resourceId),
      });
    },
  });

  const rows = useMemo(
    () =>
      filterPickerRows(
        mergePickerRows(folders.data?.items ?? [], files.data?.items ?? []),
        keyword,
      ),
    [folders.data, files.data, keyword],
  );

  const loading = folders.isLoading || files.isLoading;
  const totalLoaded = (folders.data?.items.length ?? 0) + (files.data?.items.length ?? 0);
  const hiddenCount = Math.max(
    0,
    (folders.data?.total ?? 0) + (files.data?.total ?? 0) - totalLoaded,
  );

  async function send(rowId: string, rowName: string, kind: 'folder' | 'file') {
    if (!target) return;
    setSendingId(rowId);
    try {
      await grant.mutateAsync({ resourceId: rowId, userId: target.user_id });
      message.success(sentMessage({ kind, id: rowId, name: rowName, sizeBytes: 0 }, target.username));
      onClose();
    } catch {
      message.error('发送失败：可能你在这个文件夹里没有权限，或对方账号状态异常');
    } finally {
      setSendingId(null);
    }
  }

  return (
    <ResponsiveSheet
      open={open}
      onClose={onClose}
      desktopWidth={680}
      mobileHeight="85%"
      title={
        <Space>
          <SendOutlined />
          <span>发给 {target?.username ?? ''}</span>
        </Space>
      }
    >
      <Space direction="vertical" size={10} style={{ width: '100%' }}>
        <Alert
          type="info"
          showIcon
          message="文件和文件夹都能发"
          description="点文件夹名字可以进去挑里面的文件；直接发文件夹也行 —— 对方在「分享给我的」里点开这个文件夹，就能看到并下载里面的所有东西。"
        />

        <FolderBreadcrumb
          chain={path.data ?? []}
          onNavigate={(id) => setFolderId(id)}
        />
        <Typography.Text type="secondary" style={{ fontSize: 12 }}>
          当前：{pickerLocationLabel(path.data ?? [])}
        </Typography.Text>

        <Input
          allowClear
          placeholder="在当前文件夹里按名字过滤"
          value={keyword}
          onChange={(event) => setKeyword(event.target.value)}
        />

        {loading && <Spin />}

        {!loading && rows.length === 0 && (
          <Empty
            image={Empty.PRESENTED_IMAGE_SIMPLE}
            description={keyword ? '这个文件夹里没有匹配的名字' : '这个文件夹是空的'}
          />
        )}

        <List
          dataSource={rows}
          renderItem={(row) => (
            <List.Item
              actions={[
                <Button
                  key="send"
                  type="primary"
                  loading={sendingId === row.id}
                  onClick={() => void send(row.id, row.name, row.kind)}
                >
                  {row.kind === 'folder' ? '发这个文件夹' : '发给他'}
                </Button>,
              ]}
            >
              <List.Item.Meta
                avatar={
                  row.kind === 'folder' ? (
                    <FolderOpenOutlined style={{ color: '#faad14', fontSize: 18 }} />
                  ) : (
                    <FileOutlined style={{ color: '#8c8c8c', fontSize: 18 }} />
                  )
                }
                title={
                  row.kind === 'folder' ? (
                    <a onClick={() => setFolderId(row.id)}>{row.name}</a>
                  ) : (
                    row.name
                  )
                }
                description={
                  row.kind === 'folder' ? (
                    <Tag>文件夹 · 点名字进去看里面</Tag>
                  ) : (
                    `${(row.sizeBytes / 1024 / 1024).toFixed(1)} MB`
                  )
                }
              />
            </List.Item>
          )}
        />

        {hiddenCount > 0 && (
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            这个文件夹还有 {hiddenCount} 项没列出来（一层最多显示 {PICKER_PAGE_SIZE} 项）
          </Typography.Text>
        )}
      </Space>
    </ResponsiveSheet>
  );
}
