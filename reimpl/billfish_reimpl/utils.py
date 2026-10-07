from __future__ import annotations

import hashlib
import re
from pathlib import Path


_WORD_RE = re.compile(r"[a-z0-9]+")


def file_sha1(path: Path, chunk_size: int = 1024 * 1024) -> str:
    h = hashlib.sha1()
    with path.open("rb") as f:
        while True:
            data = f.read(chunk_size)
            if not data:
                break
            h.update(data)
    return h.hexdigest()


def normalize_for_search(text: str) -> str:
    text = text.strip().lower()
    if not text:
        return ""

    tokens: list[str] = []
    latin = " ".join(_WORD_RE.findall(text))
    if latin:
        tokens.extend(latin.split())

    cjk_buffer: list[str] = []
    for ch in text:
        if _is_cjk(ch):
            cjk_buffer.append(ch)
        else:
            tokens.extend(_emit_bigrams(cjk_buffer))
            cjk_buffer.clear()
    tokens.extend(_emit_bigrams(cjk_buffer))
    return " ".join(dict.fromkeys(tokens))


def safe_ext(path: Path) -> str:
    return path.suffix.lower().lstrip(".")


def _emit_bigrams(chars: list[str]) -> list[str]:
    if not chars:
        return []
    if len(chars) == 1:
        return chars.copy()
    return ["".join(chars[i : i + 2]) for i in range(len(chars) - 1)]


def _is_cjk(ch: str) -> bool:
    code = ord(ch)
    return (
        0x4E00 <= code <= 0x9FFF
        or 0x3400 <= code <= 0x4DBF
        or 0x3040 <= code <= 0x30FF
        or 0xAC00 <= code <= 0xD7AF
    )
