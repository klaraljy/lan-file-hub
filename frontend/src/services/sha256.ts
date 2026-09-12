/**
 * 增量 SHA-256。
 *
 * 为什么不用 `crypto.subtle.digest`：它是一次性的，必须把整个文件读进内存，
 * 而单文件上限是 10 GiB（docs/storage.md）。这里实现流式 SHA-256，
 * 任何大小的文件都只占用一个分块的内存。
 *
 * 参考 FIPS 180-4。分块级哈希仍可用 `crypto.subtle`，因为分块本身有大小上限。
 */

const K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

const BLOCK_BYTES = 64;

function rotr(value: number, bits: number): number {
  return ((value >>> bits) | (value << (32 - bits))) >>> 0;
}

export class Sha256 {
  private readonly state = new Uint32Array([
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ]);

  private readonly block = new Uint8Array(BLOCK_BYTES);
  private blockLength = 0;
  private totalBytes = 0;
  private finished = false;

  update(data: Uint8Array): this {
    if (this.finished) {
      throw new Error('Sha256 已经结束，不能再写入数据');
    }
    this.totalBytes += data.length;

    let offset = 0;
    if (this.blockLength > 0) {
      const take = Math.min(BLOCK_BYTES - this.blockLength, data.length);
      this.block.set(data.subarray(0, take), this.blockLength);
      this.blockLength += take;
      offset = take;
      if (this.blockLength === BLOCK_BYTES) {
        this.compress(this.block, 0);
        this.blockLength = 0;
      }
    }

    while (offset + BLOCK_BYTES <= data.length) {
      this.compress(data, offset);
      offset += BLOCK_BYTES;
    }

    if (offset < data.length) {
      this.block.set(data.subarray(offset), 0);
      this.blockLength = data.length - offset;
    }
    return this;
  }

  /** 结束并返回 32 字节摘要；之后不能再 update */
  digest(): Uint8Array {
    if (this.finished) {
      throw new Error('Sha256 已经结束');
    }
    this.finished = true;

    // 比特长度用两个 32 位字表示（2^53 比特 = 1 PiB，够用）
    const bitLength = this.totalBytes * 8;
    const high = Math.floor(bitLength / 0x100000000);
    const low = bitLength >>> 0;

    const padded = new Uint8Array(this.blockLength < 56 ? 64 : 128);
    padded.set(this.block.subarray(0, this.blockLength), 0);
    padded[this.blockLength] = 0x80;
    const view = new DataView(padded.buffer);
    view.setUint32(padded.length - 8, high, false);
    view.setUint32(padded.length - 4, low, false);

    for (let offset = 0; offset < padded.length; offset += BLOCK_BYTES) {
      this.compress(padded, offset);
    }

    const out = new Uint8Array(32);
    const outView = new DataView(out.buffer);
    for (let i = 0; i < 8; i += 1) {
      outView.setUint32(i * 4, this.state[i]!, false);
    }
    return out;
  }

  hexDigest(): string {
    const bytes = this.digest();
    let hex = '';
    for (const byte of bytes) {
      hex += byte.toString(16).padStart(2, '0');
    }
    return hex;
  }

  private compress(data: Uint8Array, offset: number): void {
    const w = new Uint32Array(64);
    for (let i = 0; i < 16; i += 1) {
      const base = offset + i * 4;
      w[i] =
        ((data[base]! << 24) |
          (data[base + 1]! << 16) |
          (data[base + 2]! << 8) |
          data[base + 3]!) >>>
        0;
    }
    for (let i = 16; i < 64; i += 1) {
      const x = w[i - 15]!;
      const y = w[i - 2]!;
      const s0 = (rotr(x, 7) ^ rotr(x, 18) ^ (x >>> 3)) >>> 0;
      const s1 = (rotr(y, 17) ^ rotr(y, 19) ^ (y >>> 10)) >>> 0;
      w[i] = (w[i - 16]! + s0 + w[i - 7]! + s1) >>> 0;
    }

    let a = this.state[0]!;
    let b = this.state[1]!;
    let c = this.state[2]!;
    let d = this.state[3]!;
    let e = this.state[4]!;
    let f = this.state[5]!;
    let g = this.state[6]!;
    let h = this.state[7]!;

    for (let i = 0; i < 64; i += 1) {
      const s1 = (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) >>> 0;
      const ch = ((e & f) ^ (~e & g)) >>> 0;
      const temp1 = (h + s1 + ch + K[i]! + w[i]!) >>> 0;
      const s0 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) >>> 0;
      const maj = ((a & b) ^ (a & c) ^ (b & c)) >>> 0;
      const temp2 = (s0 + maj) >>> 0;

      h = g;
      g = f;
      f = e;
      e = (d + temp1) >>> 0;
      d = c;
      c = b;
      b = a;
      a = (temp1 + temp2) >>> 0;
    }

    this.state[0] = (this.state[0]! + a) >>> 0;
    this.state[1] = (this.state[1]! + b) >>> 0;
    this.state[2] = (this.state[2]! + c) >>> 0;
    this.state[3] = (this.state[3]! + d) >>> 0;
    this.state[4] = (this.state[4]! + e) >>> 0;
    this.state[5] = (this.state[5]! + f) >>> 0;
    this.state[6] = (this.state[6]! + g) >>> 0;
    this.state[7] = (this.state[7]! + h) >>> 0;
  }
}

/**
 * 读取 Blob 内容。
 * `Blob.arrayBuffer()` 在个别环境里不存在（旧浏览器、jsdom），退回 FileReader。
 */
async function readBlob(blob: Blob): Promise<Uint8Array> {
  if (typeof blob.arrayBuffer === 'function') {
    return new Uint8Array(await blob.arrayBuffer());
  }
  return new Promise<Uint8Array>((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(new Uint8Array(reader.result as ArrayBuffer));
    reader.onerror = () => reject(reader.error ?? new Error('读取文件失败'));
    reader.readAsArrayBuffer(blob);
  });
}

/** 一次性计算数据的 SHA-256（十六进制小写） */
export function sha256Hex(data: Uint8Array): string {
  return new Sha256().update(data).hexDigest();
}

/** 分块级哈希：分块有大小上限，可以整块读入 */
export async function hashBlob(blob: Blob): Promise<string> {
  return sha256Hex(await readBlob(blob));
}

export interface FileHashProgress {
  hashedBytes: number;
  totalBytes: number;
}

/**
 * 流式计算整个文件的 SHA-256：每次只读一个 slice，内存占用与文件大小无关。
 * 服务端仍会自己重算一遍，这里的值用于客户端声明与提前发现读盘错误。
 */
export async function hashFile(
  file: Blob,
  chunkBytes: number,
  onProgress?: (progress: FileHashProgress) => void,
  isCancelled?: () => boolean,
): Promise<string> {
  const hasher = new Sha256();
  const total = file.size;
  const step = Math.max(1, chunkBytes);

  for (let offset = 0; offset < total; offset += step) {
    if (isCancelled?.()) {
      throw new Error('已取消');
    }
    const slice = file.slice(offset, Math.min(offset + step, total));
    hasher.update(await readBlob(slice));
    onProgress?.({ hashedBytes: Math.min(offset + step, total), totalBytes: total });
  }

  if (total === 0) {
    onProgress?.({ hashedBytes: 0, totalBytes: 0 });
  }
  return hasher.hexDigest();
}
