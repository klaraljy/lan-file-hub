import { Breadcrumb } from 'antd';
import { HomeOutlined } from '@ant-design/icons';
import { Link } from 'react-router-dom';

import type { Folder } from '../types/folder';

/**
 * 面包屑：根目录显示为「我的文件」，其余显示名称。
 *
 * `onNavigate` 用来在**弹窗里**当下钻用（例如「发文件给某人」的选择器）：
 * 那种场景不能跳路由，只能换当前目录。
 */
export function FolderBreadcrumb({
  chain,
  onNavigate,
}: {
  chain: Folder[];
  onNavigate?: (folderId: string) => void;
}) {
  if (chain.length === 0) {
    return null;
  }

  const items = chain.map((folder, index) => {
    const isLast = index === chain.length - 1;
    const label = folder.is_root ? (
      <span>
        <HomeOutlined /> 我的文件
      </span>
    ) : (
      folder.name
    );

    const title = isLast ? (
      label
    ) : onNavigate ? (
      <a onClick={() => onNavigate(folder.id)}>{label}</a>
    ) : (
      <Link to={`/files/${folder.id}`}>{label}</Link>
    );

    return {
      key: folder.id,
      // 最后一级是当前目录，不可点击
      title,
    };
  });

  return <Breadcrumb items={items} style={{ marginBottom: 16 }} />;
}
