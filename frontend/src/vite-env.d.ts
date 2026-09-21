/**
 * vite 通过 `define` 注入的环境变量（见 vite.config.ts）。
 *
 * 注意：本项目的 tsconfig 没有引入 `vite/client` 类型，所以这里自己声明用到的键，
 * 避免 `import.meta.env` 在 tsc 下报错。
 */
interface ImportMetaEnv {
  /** 这台电脑的局域网访问地址（如 `http://192.168.43.240:5173`）；没注入时是空字符串 */
  readonly VITE_LFH_LAN_URL?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
