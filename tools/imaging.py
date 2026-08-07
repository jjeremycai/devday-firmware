#!/usr/bin/env python3
"""Image → packed 1-bit conversion for the Dev Day terminal.

The firmware draws 1-bit MSB-first row-major bitmaps directly (see
`CardContent::dash_pet` and `drawPet()` in cards.cpp). Everything that puts a
picture on the panel goes through this module.

Two conversions, because the two kinds of source want opposite treatment:

  `dither`      Floyd-Steinberg error diffusion. Right for photographs — the
                Codex profile picture — where the eye reassembles tone out of
                noise.
  `silhouette`  Alpha-derived shape plus a luminance threshold. Right for
                sprite art — Codex pets — where error diffusion turns flat
                fills into grey mud and the character stops being legible at
                96x104. Measured on the real `codex` and `gauge` atlases.

Decoding prefers Pillow and falls back to macOS `sips`, so `dash_sync.py` keeps
working on a stock machine with no pip installs (a promise the README makes).
"""

from __future__ import annotations

import io
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

# A crop box in source pixels: (left, top, width, height).
CropBox = Tuple[int, int, int, int]


class ImagingError(RuntimeError):
    """No usable image backend, or the source could not be decoded."""


# ---------------------------------------------------------------------------
# Decoding — grayscale + alpha at a target size
# ---------------------------------------------------------------------------
def _read_bmp(
    path: Path, crop: Optional[CropBox] = None, out_size: Optional[Tuple[int, int]] = None
) -> Tuple[List[int], List[int]]:
    """Parse a BMP, optionally cropping and box-downsampling, to (gray, alpha).

    Hand-rolled because the `sips` fallback exists precisely to avoid a Pillow
    dependency. Handles the 8/24/32-bit forms sips emits, both row orders.

    Crop and resize happen here rather than in `sips` because `sips -c` mangles
    the output on these sprite atlases — it wraps rows, producing a horizontally
    shifted image. Doing it in-process is both correct and cheaper (one decode,
    and only the cropped region is ever touched).

    Returned grayscale is premultiplied, matching the BGRA `sips` writes; pair
    it with `flatten_on_white()` before thresholding.
    """
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ImagingError("not a BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits not in (8, 24, 32):
        raise ImagingError(f"unsupported BMP bits={bits}")

    top_down = height < 0
    width, height = abs(width), abs(height)
    stride = ((width * bits + 31) // 32) * 4
    step = bits // 8

    left, top, cw, ch = crop if crop is not None else (0, 0, width, height)
    if left < 0 or top < 0 or left + cw > width or top + ch > height:
        raise ImagingError(
            f"crop {(left, top, cw, ch)} outside {width}x{height} image"
        )
    out_w, out_h = out_size if out_size is not None else (cw, ch)

    def sample(x: int, y: int) -> Tuple[int, int]:
        src_y = y if top_down else (height - 1 - y)
        row = pixel_offset + src_y * stride
        if bits == 8:
            return data[row + x], 255
        p = row + x * step
        b, g, r = data[p], data[p + 1], data[p + 2]
        a = data[p + 3] if bits == 32 else 255
        return int(0.299 * r + 0.587 * g + 0.114 * b), a

    gray: List[int] = [0] * (out_w * out_h)
    alpha: List[int] = [0] * (out_w * out_h)
    fx, fy = cw / out_w, ch / out_h
    for oy in range(out_h):
        y0 = top + int(oy * fy)
        y1 = max(y0 + 1, top + int((oy + 1) * fy))
        for ox in range(out_w):
            x0 = left + int(ox * fx)
            x1 = max(x0 + 1, left + int((ox + 1) * fx))
            sg = sa = n = 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    g, a = sample(xx, yy)
                    sg += g
                    sa += a
                    n += 1
            i = oy * out_w + ox
            gray[i] = sg // n
            alpha[i] = sa // n
    return gray, alpha


def _decode_sips(
    data: bytes, out_w: int, out_h: int, crop: Optional[CropBox]
) -> Tuple[List[int], List[int]]:
    if not shutil.which("sips"):
        raise ImagingError("neither Pillow nor macOS `sips` is available")
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        src = tmp_path / "src.bin"
        bmp = tmp_path / "out.bmp"
        src.write_bytes(data)
        # Format conversion only — cropping and scaling are done in _read_bmp.
        subprocess.check_call(
            ["sips", "-s", "format", "bmp", str(src), "--out", str(bmp)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return _read_bmp(bmp, crop, (out_w, out_h))


def _decode_pillow(
    data: bytes, out_w: int, out_h: int, crop: Optional[CropBox]
) -> Tuple[List[int], List[int]]:
    from PIL import Image  # type: ignore

    im = Image.open(io.BytesIO(data))
    im = im.convert("RGBA")
    if crop is not None:
        left, top, w, h = crop
        im = im.crop((left, top, left + w, top + h))
    im = im.resize((out_w, out_h), Image.Resampling.LANCZOS)
    alpha = list(im.getchannel("A").tobytes())
    gray = list(im.convert("L").tobytes())
    # Premultiply so this path agrees with the `sips` one, whose BGRA already is.
    gray = [g * a // 255 for g, a in zip(gray, alpha)]
    return gray, alpha


def decode_image(
    data: bytes, out_w: int, out_h: int, crop: Optional[CropBox] = None
) -> Tuple[List[int], List[int]]:
    """Decode to (gray, alpha) lists of out_w*out_h bytes, optionally cropping first.

    Alpha comes back straight, not premultiplied — `sips` writes premultiplied
    BGRA (transparent pixels are black), which would otherwise turn every
    transparent margin into solid ink.
    """
    try:
        return _decode_pillow(data, out_w, out_h, crop)
    except ImportError:
        pass
    except Exception as exc:  # a corrupt source should not silently fall through
        raise ImagingError(f"could not decode image: {exc}") from exc
    return _decode_sips(data, out_w, out_h, crop)


def flatten_on_white(gray: Sequence[int], alpha: Sequence[int]) -> List[int]:
    """Composite premultiplied grayscale over a white page.

    Transparent → white, opaque → unchanged. Without this, sprite atlases (which
    are transparent RGBA) come out as a black rectangle with the pet knocked out.
    """
    return [min(255, g + (255 - a)) for g, a in zip(gray, alpha)]


# ---------------------------------------------------------------------------
# Packing
# ---------------------------------------------------------------------------
def _pack(bits: Sequence[bool], w: int, h: int) -> bytearray:
    out = bytearray((w * h + 7) // 8)
    for i, on in enumerate(bits):
        if on:
            out[i >> 3] |= 0x80 >> (i & 7)
    return out


def dither(gray: Sequence[int], w: int, h: int) -> bytearray:
    """Floyd-Steinberg to packed 1-bit. Set bit = black ink."""
    buf = [float(v) for v in gray]
    bits = [False] * (w * h)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            old = buf[i]
            new = 0.0 if old < 128 else 255.0
            err = old - new
            buf[i] = new
            if new < 128:
                bits[i] = True
            if x + 1 < w:
                buf[i + 1] += err * 7 / 16
            if y + 1 < h:
                if x > 0:
                    buf[i + w - 1] += err * 3 / 16
                buf[i + w] += err * 5 / 16
                if x + 1 < w:
                    buf[i + w + 1] += err * 1 / 16
    return _pack(bits, w, h)


# Below this share of ink the art is too pale to read as a silhouette, so we
# fall back to filling the alpha shape. Keeps light-coloured pets from
# rendering as a near-empty box.
_MIN_INK = 0.15


def silhouette(
    gray: Sequence[int],
    alpha: Sequence[int],
    w: int,
    h: int,
    threshold: int = 190,
    shade: int = 70,
) -> bytearray:
    """Alpha shape + two-band luminance to packed 1-bit. Set bit = black ink.

    Pixels outside the sprite stay white whatever their colour. Inside, two
    inks: anything darker than `shade` is solid black — linework and the
    darkest features — and the band between `shade` and `threshold` renders as
    a 50% checker. One flat cutoff turned every dark-bodied pet into a solid
    blob; the checker reads as grey on the panel, so the body becomes tone and
    the character's face survives. `shade >= threshold` restores the old
    single-ink behaviour.
    """
    inside = [a > 128 for a in alpha]
    ink = [ins and g <= threshold for ins, g in zip(inside, gray)]

    shape = sum(inside)
    if shape and sum(ink) / shape < _MIN_INK:
        return _pack(inside, w, h)  # too pale to read — use the shape itself

    bits = [
        on and (g <= shade or ((i % w) + (i // w)) % 2 == 0)
        for i, (on, g) in enumerate(zip(ink, gray))
    ]
    return _pack(bits, w, h)


# ---------------------------------------------------------------------------
# Convenience wrappers
# ---------------------------------------------------------------------------
def photo_to_bits(data: bytes, w: int, h: int) -> bytearray:
    """A photograph → packed 1-bit, via error diffusion."""
    gray, alpha = decode_image(data, w, h)
    return dither(flatten_on_white(gray, alpha), w, h)


def sprite_to_bits(
    data: bytes,
    w: int,
    h: int,
    crop: Optional[CropBox] = None,
    threshold: int = 190,
    shade: int = 70,
) -> bytearray:
    """Sprite art → packed 1-bit, via two-band alpha silhouette."""
    gray, alpha = decode_image(data, w, h, crop)
    return silhouette(flatten_on_white(gray, alpha), alpha, w, h, threshold, shade)


def bits_to_pgm(bits: Sequence[int], w: int, h: int) -> bytes:
    """Render packed 1-bit back to a PGM, for eyeballing a conversion."""
    rows = []
    for y in range(h):
        row = bytearray(w)
        for x in range(w):
            i = y * w + x
            row[x] = 0 if bits[i >> 3] & (0x80 >> (i & 7)) else 255
        rows.append(bytes(row))
    return b"P5\n%d %d\n255\n" % (w, h) + b"".join(rows)
