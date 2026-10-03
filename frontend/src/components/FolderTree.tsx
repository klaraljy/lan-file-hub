/**
 * 左侧目录树（2026-10-03 用户要求："像电脑上的文件资源管理器这种，可以看哪个文件夹里面什么内容和结构"）。
 *
 * 设计要点：
 *   - **按需加载**：折叠的层级不发请求；展开哪一层才查哪一层的子目录
 *     （查询走 TanStack Query，键是 `folderKeys.children`，所以新建/改名/删除后会自动刷新）；
 *   - **跟着当前目录走**：进到深层目录时自动展开到那一层并高亮；
 *   - **手机不显示**（窄屏没地方放，手机上用面包屑 + 列表就够）。
 */
import { useEffect, useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { Spin, Typography } from 'antd';import { CaretDownOutlined, CaretRightOutlined, FolderOpenOutlined, FolderOutlined } from '@ant-design/icons';

import { folderKeys } from '../features/folders/useFolders';
import {
  TREE_PARAMS,
  ancestorIds,
  hiddenChildrenCount,
  toggleExpanded,
  treeNodeLabel,
  withExpanded,
} from '../features/folders/folderTree';
import { foldersApi } from '../services/foldersApi';
import type { Folder } from '../types/folder';

interface Props {
  /** 当前用户的根目录 */
  root: Folder | undefined;
  /** 当前所在目录 */
  currentId: string | undefined;
  /** 从根到当前目录的链路（面包屑），用来自动展开与高亮 */
  chain: Folder[];
  onSelect: (folderId: string) => void;
}

export function FolderTree({ root, currentId, chain, onSelect }: Props) {
  const [expanded, setExpanded] = useState<string[]>(() => ancestorIds(chain));

  // 切目录时把路径上的层级自动打开（只加不减，手动展开的不会被收起来）
  useEffect(() => {
    setExpanded((current) => withExpanded(current, ancestorIds(chain)));
  }, [chain]);

  if (!root) {
    return (
      <div style={{ padding: 12, textAlign: 'center' }}>
        <Spin size="small" />
      </div>
    );
  }

  return (
    <div role="tree" aria-label="目录树">
      <TreeRow
        folder={root}
        depth={0}
        currentId={currentId}
        expanded={expanded}
        onToggle={(id) => setExpanded((current) => toggleExpanded(current, id))}
        onSelect={onSelect}
      />
    </div>
  );
}

interface RowProps {
  folder: Folder;
  depth: number;
  currentId: string | undefined;
  expanded: string[];
  onToggle: (id: string) => void;
  onSelect: (folderId: string) => void;
}

function TreeRow({ folder, depth, currentId, expanded, onToggle, onSelect }: RowProps) {
  const isOpen = expanded.includes(folder.id);
  const isCurrent = currentId === folder.id;

  // 只有展开的节点才查子目录（几百个文件夹也不会一进页面就发几百个请求）
  const { data, isFetching } = useQuery({
    queryKey: folderKeys.children(folder.id, TREE_PARAMS),
    queryFn: () => foldersApi.children(folder.id, TREE_PARAMS),
    enabled: isOpen,
  });

  const children = data?.items ?? [];
  const hidden = hiddenChildrenCount(data);

  return (
    <div>
      <div
        role="treeitem"
        aria-expanded={isOpen}
        aria-selected={isCurrent}
        onClick={() => onSelect(folder.id)}
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: 4,
          // 缩进按层级走，像资源管理器一样能一眼看出深浅
          paddingLeft: 4 + depth * 14,
          paddingRight: 6,
          paddingTop: 4,
          paddingBottom: 4,
          borderRadius: 6,
          cursor: 'pointer',
          background: isCurrent ? '#e6f4ff' : undefined,
          fontWeight: isCurrent ? 600 : undefined,
        }}
      >
        <span
          // 箭头只负责展开/收起，点行本身是"进这个目录"
          onClick={(event) => {
            event.stopPropagation();
            onToggle(folder.id);
          }}
          style={{ width: 14, display: 'inline-flex', justifyContent: 'center', flex: '0 0 14px' }}
          aria-label={isOpen ? '收起' : '展开'}
        >
          {isFetching ? (
            <Spin size="small" />
          ) : isOpen ? (
            <CaretDownOutlined style={{ fontSize: 10, color: '#8c8c8c' }} />
          ) : (
            <CaretRightOutlined style={{ fontSize: 10, color: '#8c8c8c' }} />
          )}
        </span>
        {isCurrent || isOpen ? (
          <FolderOpenOutlined style={{ color: '#faad14' }} />
        ) : (
          <FolderOutlined style={{ color: '#faad14' }} />
        )}
        <Typography.Text ellipsis style={{ fontSize: 13, minWidth: 0 }}>
          {treeNodeLabel(folder)}
        </Typography.Text>
      </div>

      {isOpen && (
        <div>
          {children.length === 0 && !isFetching && (
            <div style={{ paddingLeft: 4 + (depth + 1) * 14, paddingBottom: 4 }}>
              <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                （空文件夹）
              </Typography.Text>
            </div>
          )}
          {children.map((child) => (
            <TreeRow
              key={child.id}
              folder={child}
              depth={depth + 1}
              currentId={currentId}
              expanded={expanded}
              onToggle={onToggle}
              onSelect={onSelect}
            />
          ))}
          {hidden > 0 && (
            <div style={{ paddingLeft: 4 + (depth + 1) * 14, paddingBottom: 4 }}>
              <Typography.Text type="secondary" style={{ fontSize: 12 }}>
                还有 {hidden} 个子文件夹没显示（进这个目录里看）
              </Typography.Text>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
