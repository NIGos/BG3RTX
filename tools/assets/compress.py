"""Decompression for LSPK entries and LSF sections: zlib, LZ4 (block and frame), zstd.

The `lz4` and `zstandard` packages are used when installed. Without them, LZ4 is
decoded by the pure-Python implementation below and zstd by the standard library's
`compression.zstd` (Python 3.14+), so the build needs nothing beyond CPython.
"""
from __future__ import annotations

import struct
import zlib

try:  # optional fast path
    import lz4.block as _lz4_block  # type: ignore
    import lz4.frame as _lz4_frame  # type: ignore
except ImportError:  # pragma: no cover - depends on the environment
    _lz4_block = _lz4_frame = None

# LSPK and LSF store the method in the low nibble of their flags.
METHOD_NONE, METHOD_ZLIB, METHOD_LZ4, METHOD_ZSTD = 0, 1, 2, 3

LZ4_FRAME_MAGIC = 0x184D2204


class DecompressError(ValueError):
    pass


def _lz4_block_into(src, dst: bytearray, i: int = 0, end: int | None = None) -> None:
    """Decodes one LZ4 block from src[i:end] and appends it to dst.

    Matches may reach back into what dst already holds, which is how linked
    blocks of an LZ4 frame refer to their predecessors.
    """
    n = len(src) if end is None else end
    while i < n:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        if lit:
            if i + lit > n:
                raise DecompressError("lz4: literal run past the end of the block")
            dst += src[i:i + lit]
            i += lit
        if i >= n:
            break  # the last sequence carries literals only
        if i + 2 > n:
            raise DecompressError("lz4: truncated match offset")
        off = src[i] | (src[i + 1] << 8)
        i += 2
        ml = token & 15
        if ml == 15:
            while True:
                b = src[i]
                i += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        start = len(dst) - off
        if off == 0 or start < 0:
            raise DecompressError(f"lz4: bad match offset {off}")
        if off >= ml:
            dst += dst[start:start + ml]
        else:  # overlapping copy: the last `off` bytes repeat
            pat = bytes(dst[start:])
            q, r = divmod(ml, off)
            dst += pat * q + pat[:r]


def lz4_block_decompress(src: bytes, usize: int | None = None) -> bytes:
    if _lz4_block is not None and usize is not None:
        try:
            return _lz4_block.decompress(src, uncompressed_size=usize)
        except Exception as exc:
            raise DecompressError(f"lz4: {exc}") from exc
    dst = bytearray()
    _lz4_block_into(memoryview(src).tobytes() if isinstance(src, memoryview) else src, dst)
    if usize is not None and len(dst) != usize:
        raise DecompressError(f"lz4: decoded {len(dst)} bytes, expected {usize}")
    return bytes(dst)


def lz4_frame_decompress(src: bytes) -> bytes:
    if _lz4_frame is not None:
        try:
            return _lz4_frame.decompress(src)
        except Exception as exc:
            raise DecompressError(f"lz4 frame: {exc}") from exc
    out = bytearray()
    i, n = 0, len(src)
    while i + 4 <= n:
        magic = struct.unpack_from("<I", src, i)[0]
        i += 4
        if magic & 0xFFFFFFF0 == 0x184D2A50:  # skippable frame
            size = struct.unpack_from("<I", src, i)[0]
            i += 4 + size
            continue
        if magic != LZ4_FRAME_MAGIC:
            raise DecompressError(f"lz4 frame: bad magic {magic:#x}")
        flg = src[i]
        i += 2  # FLG, BD
        if (flg >> 6) != 1:
            raise DecompressError("lz4 frame: unsupported version")
        block_checksum = bool(flg & 0x10)
        if flg & 0x08:
            i += 8  # content size
        if flg & 0x01:
            i += 4  # dictionary id
        i += 1  # header checksum
        frame_start = len(out)
        independent = bool(flg & 0x20)
        while True:
            bsize = struct.unpack_from("<I", src, i)[0]
            i += 4
            if bsize == 0:
                break
            raw = bsize & 0x80000000
            bsize &= 0x7FFFFFFF
            if raw:
                out += src[i:i + bsize]
            elif independent:
                blk = bytearray()
                _lz4_block_into(src, blk, i, i + bsize)
                out += blk
            else:
                _lz4_block_into(src, out, i, i + bsize)
            i += bsize + (4 if block_checksum else 0)
        if flg & 0x04:
            i += 4  # content checksum
        del frame_start
    return bytes(out)


def zstd_decompress(src: bytes, usize: int | None = None) -> bytes:
    try:
        from compression import zstd  # Python 3.14+
        return zstd.decompress(src)
    except ImportError:  # pragma: no cover
        pass
    try:
        import zstandard  # type: ignore
    except ImportError as e:  # pragma: no cover
        raise DecompressError("zstd needs Python 3.14+ or the zstandard package") from e
    return zstandard.ZstdDecompressor().decompress(src, max_output_size=usize or 0)


def decompress(method: int, data: bytes, usize: int, frame: bool = False) -> bytes:
    """Decodes `data` compressed with `method` (the low nibble of LSPK/LSF flags).

    `frame` selects the LZ4 frame format (LSF v2+ chunked sections) over a raw block.
    """
    if method == METHOD_NONE:
        return bytes(data)
    if method == METHOD_ZLIB:
        return zlib.decompress(data)
    if method == METHOD_LZ4:
        if frame:
            return lz4_frame_decompress(data)
        try:
            return lz4_block_decompress(data, usize)
        except DecompressError:
            # A few writers store a frame where a block is expected.
            if len(data) >= 4 and struct.unpack_from("<I", data, 0)[0] == LZ4_FRAME_MAGIC:
                return lz4_frame_decompress(data)
            raise
    if method == METHOD_ZSTD:
        return zstd_decompress(data, usize)
    raise DecompressError(f"unknown compression method {method}")
