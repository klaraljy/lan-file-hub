import { createHash } from 'node:crypto';
import { describe, expect, it } from 'vitest';

import { Sha256, hashFile, sha256Hex } from '../src/services/sha256';

/**
 * 客户端 SHA-256 是「服务端重新计算」之外的独立校验，
 * 一旦算错就会让正确的文件被判成 HASH_MISMATCH，所以用公开测试向量钉死。
 */
describe('Sha256', () => {
  const encoder = new TextEncoder();

  function hexOf(text: string): string {
    return sha256Hex(encoder.encode(text));
  }

  it('匹配 FIPS 180-4 公开向量', () => {
    expect(hexOf('')).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
    expect(hexOf('abc')).toBe(
      'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
    );
    expect(hexOf('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq')).toBe(
      '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1',
    );
  });

  it('一百万个 a 的摘要正确（多块与长度编码）', () => {
    const million = 'a'.repeat(1_000_000);
    expect(hexOf(million)).toBe(
      'cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0',
    );
  });

  it('分多次写入与一次写入结果一致', () => {
    const payload = encoder.encode('x'.repeat(1000) + '分块上传' + 'y'.repeat(77));
    const oneShot = sha256Hex(payload);

    // 故意用不规则的分片大小，覆盖缓冲区拼接的边界
    for (const step of [1, 7, 63, 64, 65, 100, 999]) {
      const hasher = new Sha256();
      for (let offset = 0; offset < payload.length; offset += step) {
        hasher.update(payload.subarray(offset, offset + step));
      }
      expect(hasher.hexDigest(), `分片大小 ${step}`).toBe(oneShot);
    }
  });

  it('与 Node 的 crypto 结果一致（随机数据）', () => {
    const random = new Uint8Array(4096);
    for (let i = 0; i < random.length; i += 1) {
      random[i] = (i * 37 + 11) % 256;
    }
    const expected = createHash('sha256').update(random).digest('hex');
    expect(sha256Hex(random)).toBe(expected);
  });

  it('结束后再写入或不带数据写入都会报错', () => {
    const hasher = new Sha256();
    hasher.update(encoder.encode('abc'));
    hasher.hexDigest();
    expect(() => hasher.update(encoder.encode('d'))).toThrow();
    expect(() => hasher.hexDigest()).toThrow();
  });

  it('恰好 55、56、64 字节的边界处理正确', () => {
    for (const size of [55, 56, 63, 64, 65, 119, 120]) {
      const payload = new Uint8Array(size).fill(size % 251);
      const expected = createHash('sha256').update(payload).digest('hex');
      expect(sha256Hex(payload), `长度 ${size}`).toBe(expected);
    }
  });
});

describe('hashFile', () => {
  it('流式哈希整个文件并报告进度', async () => {
    const content = new Uint8Array(3000);
    for (let i = 0; i < content.length; i += 1) content[i] = i % 256;
    const blob = new Blob([content]);
    const progress: number[] = [];

    const digest = await hashFile(blob, 512, (update) => progress.push(update.hashedBytes));

    expect(digest).toBe(createHash('sha256').update(content).digest('hex'));
    // 进度必须单调不减，且最后到达文件大小
    expect(progress.length).toBeGreaterThan(1);
    expect(progress[progress.length - 1]).toBe(3000);
    for (let i = 1; i < progress.length; i += 1) {
      expect(progress[i]!).toBeGreaterThanOrEqual(progress[i - 1]!);
    }
  });

  it('空文件返回空内容的摘要', async () => {
    const digest = await hashFile(new Blob([]), 512);
    expect(digest).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  });

  it('取消后抛错，不会继续读文件', async () => {
    const blob = new Blob([new Uint8Array(4096)]);
    await expect(hashFile(blob, 512, undefined, () => true)).rejects.toThrow('已取消');
  });
});
