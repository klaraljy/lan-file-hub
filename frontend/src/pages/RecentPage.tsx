/**
 * 最近文件页（docs/frontend.md §2.2 左侧导航第 2 项）。
 *
 * 说明（不要误解为功能缺失的偷懒实现）：真正按修改/访问时间跨目录聚合「最近文件」，
 * 需要一个带时间排序的跨目录列表接口；当前列表接口是按目录分页的，
 * 在前端硬凑会把「最近」做成「当前目录最近」，反而误导用户。
 * 因此这里明确标注为待后端提供接口后实现，而不是放一个假列表。
 */
import { Card, Empty, Typography } from 'antd';
import { ClockCircleOutlined } from '@ant-design/icons';

export function RecentPage() {
  return (
    <Card title="最近文件">
      <Empty
        image={Empty.PRESENTED_IMAGE_SIMPLE}
        description={
          <span>
            最近文件需要后端提供「跨目录按时间排序」的列表接口
            <br />
            <Typography.Text type="secondary" style={{ fontSize: 12 }}>
              <ClockCircleOutlined /> 在那之前请通过「我的文件」按修改时间排序查看。
            </Typography.Text>
          </span>
        }
      />
    </Card>
  );
}
