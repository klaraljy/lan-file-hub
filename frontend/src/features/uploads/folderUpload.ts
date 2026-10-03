/**
 * 「上传文件夹」（2026-10-03，用户要求）：把整个目录（含子目录）传到当前目录下，**保留目录结构**。
 *
 * 这个文件只放**纯逻辑**（路径处理、目录链、防呆上限），不碰 React、不碰网络，所以能单测；
 * 真正的 API 调用与上传在 `UploadPanel` 里（用现成的分块上传，断点续传照旧）。
 *
 * 三条安全/健壮性约束：
 *   ① 每一段目录名都做净化：拒绝空段、`.`、`..`、反斜杠与控制字符（服务器也会净化，这里是第一道）；
 *   ② 目录深度与文件数量有上限（防止误拖整个 `C:\` 把浏览器和服务端打爆）；
 *   ③ 目录**按层级从浅到深**创建，父目录一定先于子目录存在。
 */

/** 防呆上限：一次最多这么多文件 / 这么深（超过就让用户分批） */
export const MAX_FOLDER_FILES = 2000;
export const MAX_FOLDER_DEPTH = 32;

export interface PlannedFile {
  /** 浏览器给的 File 对象（上传时直接用） */
  file: File;
  /** 相对路径，例如 `扫描件/2026/报表.pdf`；普通拖文件时就是文件名 */
  relativePath: string;
  /** 目录部分（不含文件名），根 = 空字符串 */
  dirPath: string;
  /** 净化后的文件名 */
  name: string;
}

export interface FolderPlan {
  files: PlannedFile[];
  /** 需要创建的目录（从浅到深排序、去重），例如 ['扫描件', '扫描件/2026'] */
  directories: string[];
  /** 需要跳过的条目（路径不合法 / 是隐藏文件），界面上可以提示数量 */
  skipped: number;
}

/** 每一段目录名的净化：去掉路径分隔符与控制字符，拒绝 `.` / `..` / 空 */
export function sanitizeSegment(raw: string): string {
  const cleaned = (raw ?? '')
    .replace(/[\\/]/g, '_')
    // eslint-disable-next-line no-control-regex
    .replace(/[\u0000-\u001f\u007f]/g, '_')
    // Windows 上还不允许这些字符，顺手一起挡掉
    .replace(/[<>:"|?*]/g, '_')
    .trim();
  if (cleaned === '' || cleaned === '.' || cleaned === '..') return '';
  return cleaned;
}

/**
 * 把浏览器给的相对路径切成"目录段 + 文件名"。
 * 拿不到相对路径（普通拖文件）时用文件名本身，目录为空。
 */
export function splitRelativePath(relativePath: string, fallbackName: string): {
  segments: string[];
  name: string;
} | null {
  const given = (relativePath ?? '').trim();
  const raw = given.length > 0 ? relativePath : (fallbackName ?? '');
  const trimmed = raw.trim();
  if (trimmed === '') return null;
  // 以分隔符结尾 = 只有目录没有文件名（例如 "a/"）：这种输入不该被当成"文件叫 a"
  if (/[\\/]\s*$/.test(trimmed)) return null;
  // webkitRelativePath 用 '/' 分隔；某些系统（或压缩包解出来的名字）会带 '\'
  const parts = trimmed.split(/[\\/]+/).filter((part) => part.length > 0);
  if (parts.length === 0) return null;
  const name = sanitizeSegment(parts[parts.length - 1] ?? '');
  if (name === '') return null;
  const segments: string[] = [];
  for (const part of parts.slice(0, -1)) {
    const clean = sanitizeSegment(part);
    if (clean === '') return null;  // 中间某段不合法 → 整个文件跳过（不许静默补救成别的目录）
    segments.push(clean);
  }
  if (segments.length > MAX_FOLDER_DEPTH) return null;
  return { segments, name };
}

/** 由文件列表算出"要传哪些、要建哪些目录"（目录按浅→深排序） */
export function planFolderUpload(files: File[]): FolderPlan {
  const planned: PlannedFile[] = [];
  const directories = new Set<string>();
  let skipped = 0;

  for (const file of files) {
    const relativePath = (file as File & { webkitRelativePath?: string }).webkitRelativePath ?? '';
    const split = splitRelativePath(relativePath, file.name);
    if (!split) {
      skipped++;
      continue;
    }
    const dirPath = split.segments.join('/');
    if (dirPath !== '') {
      // 每一级父目录都要建（a/b/c → a、a/b、a/b/c）
      let prefix = '';
      for (const segment of split.segments) {
        prefix = prefix === '' ? segment : `${prefix}/${segment}`;
        directories.add(prefix);
      }
    }
    // 用净化后的名字替换原文件名（File 不可变，上传时用 name 覆盖）
    planned.push({ file, relativePath: relativePath || file.name, dirPath, name: split.name });
    if (planned.length > MAX_FOLDER_FILES) {
      skipped++;
      planned.pop();
      break;
    }
  }

  return {
    files: planned,
    directories: [...directories].sort((left, right) => {
      const byDepth = left.split('/').length - right.split('/').length;
      return byDepth !== 0 ? byDepth : left.localeCompare(right);
    }),
    skipped,
  };
}

/** 目录总数与总体积（确认框里给用户看：别点一下就传走整个盘） */
export function summarizePlan(plan: FolderPlan): { files: number; directories: number; bytes: number } {
  let bytes = 0;
  for (const item of plan.files) {
    bytes += Math.max(0, item.file.size);
  }
  return { files: plan.files.length, directories: plan.directories.length, bytes };
}

/** 递归读拖进来的目录（`webkitGetAsEntry`），返回扁平的文件数组（每个 File 带 webkitRelativePath 语义） */
export async function readDroppedEntries(items: DataTransferItemList): Promise<File[]> {
  const roots: FileSystemEntry[] = [];
  for (let index = 0; index < items.length; index++) {
    const item = items[index];
    if (!item || item.kind !== 'file') continue;
    const entry = typeof item.webkitGetAsEntry === 'function' ? item.webkitGetAsEntry() : null;
    if (entry) roots.push(entry);
  }
  const files: File[] = [];
  for (const entry of roots) {
    await collectEntry(entry, '', files);
  }
  return files;
}

async function collectEntry(entry: FileSystemEntry, prefix: string, out: File[]): Promise<void> {
  if (entry.isFile) {
    const file = await fileFromEntry(entry as FileSystemFileEntry);
    if (!file) return;
    // 拖进来的文件没有 webkitRelativePath，这里手动补上相对路径，好让 planFolderUpload 用同一套逻辑
    Object.defineProperty(file, 'webkitRelativePath', {
      value: prefix === '' ? file.name : `${prefix}/${file.name}`,
      configurable: true,
    });
    out.push(file);
    return;
  }
  if (entry.isDirectory) {
    const reader = (entry as FileSystemDirectoryEntry).createReader();
    const dirPrefix = prefix === '' ? entry.name : `${prefix}/${entry.name}`;
    for (;;) {
      const batch = await readEntries(reader);
      if (batch.length === 0) break;  // 规范要求读到空数组才算读完
      for (const child of batch) {
        await collectEntry(child, dirPrefix, out);
      }
    }
  }
}

function fileFromEntry(entry: FileSystemFileEntry): Promise<File | null> {
  return new Promise((resolve) => {
    entry.file(
      (file) => resolve(file),
      () => resolve(null),
    );
  });
}

function readEntries(reader: FileSystemDirectoryReader): Promise<FileSystemEntry[]> {
  return new Promise((resolve) => {
    reader.readEntries(
      (entries) => resolve(entries),
      () => resolve([]),
    );
  });
}
