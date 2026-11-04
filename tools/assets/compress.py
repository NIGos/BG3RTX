"""Decompression for LSPK entries and LSF sections: zlib, LZ4 (block and frame), zstd.

The `lz4` and `zstandard` packages are used when installed. Without them, LZ4 is
decoded by the pure-Python implementation below and zstd by the standard library's
`compression.zstd` (Python 3.14+), so the build needs nothing beyond CPython.
"""
from __future__ import annotations

import io
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


def _lz4_block_into(src, dst: bytearray, i: int = 0, end: int | None = None, limit: int | None = None) -> None:
    """Decodes one LZ4 block from src[i:end] and appends it to dst.

    Matches may reach back into what dst already holds, which is how linked
    blocks of an LZ4 frame refer to their predecessors.
    """
    n = len(src) if end is None else end
    if n > len(src) or i < 0:
        raise DecompressError("lz4: truncated block")
    while i < n:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                if i >= n:
                    raise DecompressError("lz4: truncated length")
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        if limit is not None and len(dst) + lit > limit:
            raise DecompressError("lz4: output exceeds expected size")
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
                if i >= n:
                    raise DecompressError("lz4: truncated length")
                b = src[i]
                i += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        if limit is not None and len(dst) + ml > limit:
            raise DecompressError("lz4: output exceeds expected size")
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
            out = _lz4_block.decompress(src, uncompressed_size=usize)
            if len(out) != usize:
                raise DecompressError("lz4: decoded size mismatch")
            return out
        except Exception as exc:
            raise DecompressError(f"lz4: {exc}") from exc
    dst = bytearray()
    _lz4_block_into(memoryview(src).tobytes() if isinstance(src, memoryview) else src, dst, limit=usize)
    if usize is not None and len(dst) != usize:
        raise DecompressError(f"lz4: decoded {len(dst)} bytes, expected {usize}")
    return bytes(dst)


def lz4_frame_decompress(src: bytes, usize: int | None = None) -> bytes:
    if _lz4_frame is not None:
        try:
            if usize is None:
                out = _lz4_frame.decompress(src)
            else:
                decoder = _lz4_frame.LZ4FrameDecompressor()
                out = decoder.decompress(src, max_length=usize + 1)
                if not decoder.eof or decoder.unused_data:
                    raise DecompressError("lz4 frame: truncated, trailing data or output exceeds expected size")
            if usize is not None and len(out) != usize:
                raise DecompressError("lz4 frame: decoded size mismatch")
            return out
        except Exception as exc:
            raise DecompressError(f"lz4 frame: {exc}") from exc
    out = bytearray()
    i, n = 0, len(src)
    def require(end):
        if end > n:
            raise DecompressError("lz4 frame: truncated input")
    while i < n:
        require(i + 4)
        magic = struct.unpack_from("<I", src, i)[0]
        i += 4
        if magic & 0xFFFFFFF0 == 0x184D2A50:  # skippable frame
            require(i + 4)
            size = struct.unpack_from("<I", src, i)[0]
            i += 4 + size
            require(i)
            continue
        if magic != LZ4_FRAME_MAGIC:
            raise DecompressError(f"lz4 frame: bad magic {magic:#x}")
        require(i + 2)
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
        require(i)
        independent = bool(flg & 0x20)
        while True:
            require(i + 4)
            bsize = struct.unpack_from("<I", src, i)[0]
            i += 4
            if bsize == 0:
                break
            raw = bsize & 0x80000000
            bsize &= 0x7FFFFFFF
            require(i + bsize + (4 if block_checksum else 0))
            if usize is not None and len(out) + (bsize if raw else 0) > usize:
                raise DecompressError("lz4 frame: output exceeds expected size")
            if raw:
                out += src[i:i + bsize]
            elif independent:
                blk = bytearray()
                _lz4_block_into(src, blk, i, i + bsize, None if usize is None else usize - len(out))
                out += blk
            else:
                _lz4_block_into(src, out, i, i + bsize, usize)
            i += bsize + (4 if block_checksum else 0)
        if flg & 0x04:
            i += 4  # content checksum
        require(i)
    if usize is not None and len(out) != usize:
        raise DecompressError("lz4 frame: decoded size mismatch")
    return bytes(out)


def zstd_decompress(src: bytes, usize: int | None = None) -> bytes:
    try:
        from compression import zstd  # Python 3.14+
        if usize is None:
            return zstd.decompress(src)
        decoder = zstd.ZstdDecompressor()
        out = decoder.decompress(src, max_length=usize + 1)
        if not decoder.eof or decoder.unused_data or len(out) != usize:
            raise DecompressError("zstd: truncated, trailing data or decoded size mismatch")
        return out
    except ImportError:  # pragma: no cover
        pass
    try:
        import zstandard  # type: ignore
    except ImportError as e:  # pragma: no cover
        raise DecompressError("zstd needs Python 3.14+ or the zstandard package") from e
    with zstandard.ZstdDecompressor().stream_reader(io.BytesIO(src)) as reader:
        out = reader.read(-1 if usize is None else usize + 1)
    if usize is not None and len(out) != usize:
        raise DecompressError("zstd: decoded size mismatch")
    return out


def decompress(method: int, data: bytes, usize: int, frame: bool = False) -> bytes:
    """Decodes `data` compressed with `method` (the low nibble of LSPK/LSF flags).

    `frame` selects the LZ4 frame format (LSF v2+ chunked sections) over a raw block.
    """
    if usize < 0:
        raise DecompressError("negative decoded size")
    try:
        if method == METHOD_NONE:
            out = bytes(data)
        elif method == METHOD_ZLIB:
            decoder = zlib.decompressobj()
            out = decoder.decompress(data, usize + 1)
            if not decoder.eof or decoder.unconsumed_tail:
                raise DecompressError("zlib: truncated stream or output exceeds expected size")
        elif method == METHOD_LZ4:
            if frame or data[:4] == struct.pack("<I", LZ4_FRAME_MAGIC):
                out = lz4_frame_decompress(data, usize)
            else:
                out = lz4_block_decompress(data, usize)
        elif method == METHOD_ZSTD:
            out = zstd_decompress(data, usize)
        else:
            raise DecompressError(f"unknown compression method {method}")
    except (IndexError, struct.error, zlib.error) as exc:
        raise DecompressError(str(exc)) from exc
    if len(out) != usize:
        raise DecompressError(f"decoded {len(out)} bytes, expected {usize}")
    return out
