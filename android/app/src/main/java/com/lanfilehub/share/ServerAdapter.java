package com.lanfilehub.share;

import android.content.Context;
import android.graphics.Color;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * 服务器列表：每条显示"名字 / 地址 / 在不在线"。
 *
 * <p>在线状态用颜色直接表达（绿=在线、灰=离线、深灰=正在检查），不懂技术的人也能一眼看懂。
 */
public final class ServerAdapter extends BaseAdapter {

  /** 未知 = 还没探过（刚打开页面那一瞬间） */
  public enum Status {
    UNKNOWN,
    ONLINE,
    OFFLINE
  }

  private final Context context;
  private final List<ServerStore.Server> servers = new ArrayList<>();
  private final Map<String, Status> statuses = new HashMap<>();

  public ServerAdapter(Context context) {
    this.context = context;
  }

  public void setServers(List<ServerStore.Server> items) {
    servers.clear();
    servers.addAll(items);
    notifyDataSetChanged();
  }

  public void setStatus(ServerStore.Server server, Status status) {
    statuses.put(server.key(), status);
    notifyDataSetChanged();
  }

  public void clearStatuses() {
    statuses.clear();
    notifyDataSetChanged();
  }

  public List<ServerStore.Server> getServers() {
    return new ArrayList<>(servers);
  }

  @Override
  public int getCount() {
    return servers.size();
  }

  @Override
  public Object getItem(int position) {
    return servers.get(position);
  }

  @Override
  public long getItemId(int position) {
    return position;
  }

  @Override
  public View getView(int position, View convertView, ViewGroup parent) {
    View row = convertView;
    if (row == null) {
      row = LayoutInflater.from(context).inflate(R.layout.item_server, parent, false);
    }
    ServerStore.Server server = servers.get(position);
    Status status = statuses.containsKey(server.key()) ? statuses.get(server.key()) : Status.UNKNOWN;

    TextView name = row.findViewById(R.id.server_name);
    TextView url = row.findViewById(R.id.server_url);
    TextView state = row.findViewById(R.id.server_status);

    name.setText(server.displayName());
    url.setText(server.baseUrl());
    switch (status) {
      case ONLINE:
        state.setText(R.string.connect_online);
        state.setTextColor(Color.parseColor("#1f9d55"));
        break;
      case OFFLINE:
        state.setText(R.string.connect_offline);
        state.setTextColor(Color.parseColor("#999999"));
        break;
      default:
        state.setText(R.string.connect_checking);
        state.setTextColor(Color.parseColor("#666666"));
        break;
    }
    return row;
  }
}
