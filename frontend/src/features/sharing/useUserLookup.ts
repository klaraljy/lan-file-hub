/**
 * 「发送给…」的选人查询（AGENTS.md §14.3）。
 *
 * 关键词至少 2 个字符才发请求，并做 300ms 防抖 —— 否则用户每敲一个字就打一次后端。
 * 判定与结果条数都由后端负责（排除自己、排除被禁用用户、最多 10 条）。
 */
import { useEffect, useState } from 'react';
import { useQuery } from '@tanstack/react-query';

import { usersApi } from '../../services/usersApi';

export const MIN_KEYWORD_LENGTH = 2;
const DEBOUNCE_MS = 300;

export const userLookupKeys = {
  all: ['users', 'lookup'] as const,
  keyword: (keyword: string) => [...userLookupKeys.all, keyword] as const,
};

export function useUserLookup(keyword: string) {
  const trimmed = keyword.trim();
  const [debounced, setDebounced] = useState(trimmed);

  useEffect(() => {
    const timer = window.setTimeout(() => setDebounced(trimmed), DEBOUNCE_MS);
    return () => window.clearTimeout(timer);
  }, [trimmed]);

  // 两个条件都要看：用户把字删短时，debounced 还会短暂停留在旧值上
  const ready = debounced.length >= MIN_KEYWORD_LENGTH && trimmed.length >= MIN_KEYWORD_LENGTH;

  const query = useQuery({
    queryKey: userLookupKeys.keyword(debounced),
    queryFn: () => usersApi.lookupUsers(debounced),
    enabled: ready,
    staleTime: 30_000,
  });

  return { ...query, ready, keyword: debounced };
}
