import { Spin } from 'antd';
import { Navigate, Route, Routes, useLocation } from 'react-router-dom';
import type { ReactElement } from 'react';

import { AppLayout } from './app/AppLayout';
import { AdminPage } from './pages/AdminPage';
import { useAuth } from './features/auth/AuthContext';
import { FilesPage } from './pages/FilesPage';
import { LoginPage } from './pages/LoginPage';
import { MyPage } from './pages/MyPage';
import { PlazaPage } from './pages/PlazaPage';
import { PublicSharePage } from './pages/PublicSharePage';
import { RecentPage } from './pages/RecentPage';
import { RegisterPage } from './pages/RegisterPage';
import { SettingsPage } from './pages/SettingsPage';
import { SharedWithMePage } from './pages/SharedWithMePage';
import { TrashPage } from './pages/TrashPage';

/** 受保护路由：未登录跳登录页，并记住来源以便登录后返回 */
function RequireAuth({ children }: { children: ReactElement }) {
  const { status } = useAuth();
  const location = useLocation();

  if (status === 'restoring') {
    // 正在用已保存的 Token 恢复登录态：不要闪现登录页
    return (
      <div style={{ display: 'flex', justifyContent: 'center', paddingTop: 120 }}>
        <Spin size="large" tip="正在恢复登录状态…">
          <div style={{ width: 160, height: 60 }} />
        </Spin>
      </div>
    );
  }

  if (status === 'anonymous') {
    return <Navigate to="/login" replace state={{ from: location.pathname }} />;
  }

  return children;
}

/** 已登录用户不应再看到登录/注册页 */
function RedirectIfAuthenticated({ children }: { children: ReactElement }) {
  const { status } = useAuth();
  if (status === 'authenticated') {
    return <Navigate to="/files" replace />;
  }
  return children;
}

export default function App() {
  return (
    <Routes>
      {/* 公开分享落地页：不需要登录，因此**不能**包在 RequireAuth 里 */}
      <Route path="/s/:token" element={<PublicSharePage />} />

      <Route
        path="/login"
        element={
          <RedirectIfAuthenticated>
            <LoginPage />
          </RedirectIfAuthenticated>
        }
      />
      <Route
        path="/register"
        element={
          <RedirectIfAuthenticated>
            <RegisterPage />
          </RedirectIfAuthenticated>
        }
      />

      {/* 登录后的所有页面共用同一个外壳（左侧导航 + 顶部用户信息） */}
      <Route
        element={
          <RequireAuth>
            <AppLayout />
          </RequireAuth>
        }
      >
        <Route path="/files" element={<FilesPage />} />
        <Route path="/files/:folderId" element={<FilesPage />} />
        {/* 手机底部 Tab 的第三个入口：最近文件/回收站/设置/退出登录都收在这里 */}
        <Route path="/me" element={<MyPage />} />
        {/* 广场：谁在线、直接发文件给他（阶段 ⑤） */}
        <Route path="/plaza" element={<PlazaPage />} />
        <Route path="/recent" element={<RecentPage />} />
        <Route path="/shared" element={<SharedWithMePage />} />
        <Route path="/trash" element={<TrashPage />} />
        <Route path="/settings" element={<SettingsPage />} />
        <Route path="/admin" element={<AdminPage />} />
      </Route>

      <Route path="*" element={<Navigate to="/files" replace />} />
    </Routes>
  );
}
