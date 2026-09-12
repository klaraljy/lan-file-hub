import { Breadcrumb } from 'antd';
import { HomeOutlined } from '@ant-design/icons';
import { Link } from 'react-router-dom';

import type { Folder } from '../types/folder';

/** 面包屑：根目录显示为「我的文件」，其余显示名称 */
export function FolderBreadcrumb({ chain }: { chain: Folder[] }) {
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

    return {
      key: folder.id,
      // 最后一级是当前目录，不可点击
      title: isLast ? label : <Link to={`/files/${folder.id}`}>{label}</Link>,
    };
  });

  return <Breadcrumb items={items} style={{ marginBottom: 16 }} />;
}
