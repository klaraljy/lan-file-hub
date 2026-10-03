import { describe, expect, it } from 'vitest';
import { act } from 'react';
import { createRoot } from 'react-dom/client';

import { FileCardList } from '../src/components/FileCardList';
import { FileGrid } from '../src/components/FileGrid';
import type { BrowseRow } from '../src/features/files/listView';
import type { FileMeta } from '../src/types/file';
import type { Folder } from '../src/types/folder';

/**
 * 把按钮**真的渲染出来**再断言 DOM（2026-10-03）。
 *
 * 用户投诉原话："手机端更是一大个发送给按钮，我的下载按钮呢"、"按钮的颜色和比例不舒服"。
 * 纯函数测试证明不了这件事 —— 必须看渲染结果：
 *   - 一行里**只有一个实心主按钮**，而且它是「下载」（文件）/「打开」（文件夹）；
 *   - 「发送给…」还在行上（AGENTS §14.3），但不再是主按钮；
 *   - 一行总共就三个控件（主按钮 + 发送给… + 更多），不是以前的七八个。
 */

function folder(id: string, name: string): Folder {
  return {
    id,
    name,
    parent_id: 'root',
    owner_id: 'user-1',
    is_root: false,
    created_at: '2026-01-01T00:00:00Z',
    updated_at: '2026-01-01T00:00:00Z',
  } as Folder;
}

function file(id: string, name: string, mime = 'video/mp4'): FileMeta {
  return {
    id,
    owner_id: 'user-1',
    folder_id: 'folder-1',
    current_version_id: 'v1',
    name,
    mime_type: mime,
    size_bytes: 1024 * 1024,
    content_hash: 'a'.repeat(64),
    status: 'ACTIVE',
    created_at: '2026-01-01T00:00:00Z',
    updated_at: '2026-01-01T00:00:00Z',
    deleted_at: null,
  } as FileMeta;
}

function fileRow(name = 'EP1.mp4'): BrowseRow {
  const meta = file('f1', name);
  return {
    key: 'file-f1',
    kind: 'file',
    name,
    size_bytes: meta.size_bytes,
    updated_at: meta.updated_at,
    content_hash: meta.content_hash,
    file: meta,
  };
}

function folderRow(name = '原版'): BrowseRow {
  const meta = folder('d1', name);
  return {
    key: 'folder-d1',
    kind: 'folder',
    name,
    size_bytes: 0,
    updated_at: meta.updated_at,
    folder: meta,
  };
}

async function render(element: React.ReactElement): Promise<HTMLDivElement> {
  const container = document.createElement('div');
  document.body.appendChild(container);
  const root = createRoot(container);
  await act(async () => {
    root.render(element);
  });
  return container;
}

const noop = () => undefined;

function textOf(button: Element | null | undefined): string {
  return (button?.textContent ?? '').replace(/\s+/g, '');
}

describe('手机卡片（FileCardList）的操作区', () => {
  it('主按钮是「下载」，发送给…退到第二行（用户投诉的核心）', async () => {
    const container = await render(
      <FileCardList
        rows={[fileRow('EP1.mp4')]}
        onOpenFolder={noop}
        onRename={noop}
        onDownload={noop}
        onDetail={noop}
        onPreview={noop}
        onVersions={noop}
        onShare={noop}
        onPermission={noop}
        onSend={noop}
        onDownloadFolder={noop}
        onDelete={noop}
      />,
    );

    const buttons = Array.from(container.querySelectorAll('button'));
    // 第一个按钮就是主按钮，而且是「下载」
    expect(textOf(buttons[0])).toContain('下载');
    expect(buttons[0]!.className).toContain('ant-btn-primary');
    // 「发送给…」还在行上，但不再是 primary
    const send = buttons.find((button) => textOf(button).includes('发送给'));
    expect(send).toBeTruthy();
    expect(send!.className).not.toContain('ant-btn-primary');
    // 一屏一张卡：主按钮只能是这一个
    expect(container.querySelectorAll('button.ant-btn-primary')).toHaveLength(1);
    // 不再是一排按钮：主按钮 + 发送给… + 更多 = 3 个
    expect(buttons).toHaveLength(3);
  });

  it('文件夹卡片的主按钮是「下载」（下载里面的东西，用户 2026-10-03 要求）', async () => {
    const container = await render(
      <FileCardList
        rows={[folderRow('原版')]}
        onOpenFolder={noop}
        onRename={noop}
        onDownload={noop}
        onDetail={noop}
        onPreview={noop}
        onVersions={noop}
        onShare={noop}
        onPermission={noop}
        onSend={noop}
        onDownloadFolder={noop}
        onDelete={noop}
      />,
    );

    const primary = container.querySelector('button.ant-btn-primary');
    expect(textOf(primary)).toContain('下载');
    // 文件夹卡片：主按钮（下载）+ 发送给… + 更多 = 3 个控件（「打开」是点整张卡）
    expect(container.querySelectorAll('button')).toHaveLength(3);
  });
});

describe('桌面网格（FileGrid）的操作区', () => {
  it('每张卡也就三个控件，主按钮是描边款（一屏几十张卡不会糊成一片蓝）', async () => {
    const container = await render(
      <FileGrid
        rows={[fileRow('EP1.mp4')]}
        onOpenFolder={noop}
        onDownload={noop}
        onDetail={noop}
        onPreview={noop}
        onVersions={noop}
        onShare={noop}
        onSend={noop}
        onDownloadFolder={noop}
        onDelete={noop}
      />,
    );

    const buttons = Array.from(container.querySelectorAll('button'));
    expect(buttons).toHaveLength(3);
    expect(textOf(buttons[0])).toContain('下载');
    expect(buttons[0]!.className).toContain('ant-btn-background-ghost');
  });
});
