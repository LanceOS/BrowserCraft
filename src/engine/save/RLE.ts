export function compressRLE(src: Uint8Array, dst: Uint8Array): number {
  let srcIdx = 0;
  let dstIdx = 0;

  while (srcIdx < src.length) {
    let runLen = 1;
    while (srcIdx + runLen < src.length && src[srcIdx + runLen] === src[srcIdx] && runLen < 130) {
      runLen++;
    }

    if (runLen >= 3) {
      dst[dstIdx++] = 128 + (runLen - 3);
      dst[dstIdx++] = src[srcIdx];
      srcIdx += runLen;
      continue;
    }

    const literalStart = srcIdx;
    srcIdx += runLen;
    while (srcIdx < src.length) {
      runLen = 1;
      while (srcIdx + runLen < src.length && src[srcIdx + runLen] === src[srcIdx] && runLen < 130) {
        runLen++;
      }

      const literalLen = srcIdx - literalStart;
      if (runLen >= 3 || literalLen >= 128) break;
      srcIdx += runLen;
    }

    const literalLen = srcIdx - literalStart;
    dst[dstIdx++] = literalLen - 1;
    dst.set(src.subarray(literalStart, literalStart + literalLen), dstIdx);
    dstIdx += literalLen;
  }

  return dstIdx;
}

export function decompressRLE(src: Uint8Array, dst: Uint8Array): void {
  let srcIdx = 0;
  let dstIdx = 0;

  while (srcIdx < src.length && dstIdx < dst.length) {
    const control = src[srcIdx++];
    if (control < 128) {
      const literalLen = control + 1;
      dst.set(src.subarray(srcIdx, srcIdx + literalLen), dstIdx);
      srcIdx += literalLen;
      dstIdx += literalLen;
      continue;
    }

    const runLen = control - 128 + 3;
    const value = src[srcIdx++];
    dst.fill(value, dstIdx, dstIdx + runLen);
    dstIdx += runLen;
  }

  if (dstIdx !== dst.length) {
    throw new Error("Corrupt RLE payload");
  }
}
