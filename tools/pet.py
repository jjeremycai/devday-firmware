#!/usr/bin/env python3
"""Codex pet discovery and conversion for the Dev Day terminal.

A Codex pet ships as a sprite atlas: a grid of transparent RGBA cells, one per
animation frame. The terminal imports the first two idle cells in place of the
profile picture, so the job here is to find a pet, pull cells out of its sheet,
and hand back packed 1-bit rows the firmware can blit.

Atlas geometry, from the pet contract in the Codex CLI's `hatch-pet` skill:

  cell        192x208
  rows 0-8    idle, running-right, running-left, waving, jumping, failed,
              waiting, running, review
  rows 9-10   16 look directions (v2 sheets only)

Row 0 column 0 is idle's first frame, which the contract designates the
reduced-motion still — the right choice for a display that redraws in seconds.
Both the built-in `codex` sheet and locally hatched pets measure 8 columns of
192x208; the grid is derived from the sheet rather than assumed, so a future
layout still lands on the right cell.
"""

from __future__ import annotations

import json
import os
import urllib.request
from pathlib import Path
from typing import List, Optional, Tuple

import imaging

# The pet's box on the Usage card. Keeps the 192:208 cell aspect, so nothing is
# stretched. Must match PET_W/PET_H in firmware/content.h.
PET_W = 96
PET_H = 104
PET_BYTES = (PET_W * PET_H) // 8

CELL_W = 192
CELL_H = 208

CODEX_HOME = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))
LOCAL_PETS = CODEX_HOME / "pets"
CACHED_PETS = CODEX_HOME / "cache" / "tui-pets" / "v1"

# Where the CLI itself fetches built-in pets. `codex` is "The original Codex
# companion" — the default every attendee will recognise.
CDN_BASE = "https://persistent.oaistatic.com/codex/pets/v1"
DEFAULT_PET = "codex"
BUILTIN_PETS = (
    "codex", "dewey", "fireball", "rocky", "seedy", "stacky", "bsod", "null-signal",
)


# What `tui.pet` can be set to to turn pets off in Codex. Someone who did that
# does not want one on their desk either.
DISABLE_WORDS = frozenset(
    {"disable", "disabled", "hide", "hidden", "off", "none"}
)


class PetError(RuntimeError):
    """No pet could be resolved, or its sheet was unusable."""


def pet_is_disabled(value: Optional[str]) -> bool:
    return bool(value) and value.strip().lower() in DISABLE_WORDS


def _image_size(data: bytes) -> Tuple[int, int]:
    try:
        from PIL import Image  # type: ignore
        import io

        with Image.open(io.BytesIO(data)) as im:
            return im.size
    except ImportError:
        pass

    import shutil
    import subprocess
    import tempfile

    if not shutil.which("sips"):
        raise PetError("need Pillow or macOS `sips` to read the spritesheet")
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "sheet.bin"
        src.write_bytes(data)
        out = subprocess.run(
            ["sips", "-g", "pixelWidth", "-g", "pixelHeight", str(src)],
            capture_output=True,
            text=True,
        ).stdout
    dims = {}
    for line in out.splitlines():
        if ":" in line:
            k, _, v = line.strip().partition(":")
            if k in ("pixelWidth", "pixelHeight"):
                dims[k] = int(v.strip())
    if "pixelWidth" not in dims or "pixelHeight" not in dims:
        raise PetError("could not read spritesheet dimensions")
    return dims["pixelWidth"], dims["pixelHeight"]


def cell_box(sheet: bytes, row: int = 0, col: int = 0) -> imaging.CropBox:
    """The crop box for one atlas cell, with the grid derived from the sheet."""
    w, h = _image_size(sheet)
    if w % CELL_W or h % CELL_H:
        raise PetError(
            f"spritesheet {w}x{h} is not a whole number of {CELL_W}x{CELL_H} cells"
        )
    cols, rows = w // CELL_W, h // CELL_H
    if not (0 <= col < cols and 0 <= row < rows):
        raise PetError(f"cell ({row},{col}) outside a {rows}x{cols} grid")
    return (col * CELL_W, row * CELL_H, CELL_W, CELL_H)


def sheet_bits(
    sheet: bytes, row: int = 0, col: int = 0, threshold: int = 170
) -> bytearray:
    """One atlas cell → packed 1-bit rows, PET_W x PET_H.

    Sprite art gets the dark-display silhouette treatment: a white alpha edge,
    solid light details, and a checker for mid-tones. This keeps a terminal
    pet's face black while its cursor and outline remain visible.
    """
    return imaging.sprite_to_dark_bits(
        sheet, PET_W, PET_H, cell_box(sheet, row, col), light=threshold
    )


# ---------------------------------------------------------------------------
# Finding a pet
# ---------------------------------------------------------------------------
def _read_package(directory: Path) -> Optional[bytes]:
    """A local pet package: pet.json naming a spritesheet beside it."""
    manifest = directory / "pet.json"
    if not manifest.is_file():
        return None
    try:
        meta = json.loads(manifest.read_text())
    except (OSError, ValueError):
        return None
    name = meta.get("spritesheetPath") or "spritesheet.webp"
    # Never let a manifest reach outside its own directory.
    sheet = (directory / name).resolve()
    if directory.resolve() not in sheet.parents or not sheet.is_file():
        return None
    return sheet.read_bytes()


def _download(pet_id: str, timeout: float = 20.0) -> bytes:
    url = f"{CDN_BASE}/{pet_id}-spritesheet-v4.webp"
    req = urllib.request.Request(url, headers={"User-Agent": "devday-terminal"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def list_local_pets() -> List[str]:
    if not LOCAL_PETS.is_dir():
        return []
    return sorted(p.name for p in LOCAL_PETS.iterdir() if (p / "pet.json").is_file())


def resolve_pet(want: Optional[str] = None, allow_download: bool = True) -> Tuple[str, bytes]:
    """Find a pet and return (label, spritesheet bytes).

    Order: an explicit path or id, then a locally hatched pet, then the CLI's
    own download cache, then the built-in sheet from the CDN. Every step is
    local until the last, so a machine that has ever opened the pet picker never
    reaches the network.
    """
    candidates: List[str] = []
    if want:
        path = Path(want).expanduser()
        if path.is_dir():
            sheet = _read_package(path)
            if sheet is None:
                raise PetError(f"{path} has no readable pet.json + spritesheet")
            return path.name, sheet
        if path.is_file():
            return path.stem, path.read_bytes()
        candidates.append(want)
    else:
        candidates.extend(list_local_pets())
        candidates.append(DEFAULT_PET)

    for pet_id in candidates:
        sheet = _read_package(LOCAL_PETS / pet_id)
        if sheet is not None:
            return pet_id, sheet
        for cached in (
            CACHED_PETS / f"{pet_id}-spritesheet-v4.webp",
            CACHED_PETS / pet_id / "spritesheet.webp",
        ):
            if cached.is_file():
                return pet_id, cached.read_bytes()

    if allow_download:
        pet_id = candidates[-1] if candidates else DEFAULT_PET
        if pet_id in BUILTIN_PETS:
            try:
                return pet_id, _download(pet_id)
            except Exception as exc:
                raise PetError(f"could not download the {pet_id} pet: {exc}") from exc

    raise PetError(f"no pet found for {want!r}")


def load_pet_bits(
    want: Optional[str] = None,
    row: int = 0,
    col: int = 0,
    allow_download: bool = True,
) -> Tuple[str, bytearray]:
    """Resolve a pet and convert one frame. Returns (label, packed bits)."""
    label, sheet = resolve_pet(want, allow_download)
    return label, sheet_bits(sheet, row, col)
