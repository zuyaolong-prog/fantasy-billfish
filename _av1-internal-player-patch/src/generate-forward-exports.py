from __future__ import annotations

import pathlib
import sys

import pefile


HOOK_EXPORTS = {
    "avcodec_alloc_context3",
    "avcodec_close",
    "avcodec_find_decoder",
    "avcodec_find_decoder_by_name",
    "avcodec_flush_buffers",
    "avcodec_free_context",
    "avcodec_get_hw_config",
    "avcodec_open2",
    "avcodec_receive_frame",
    "avcodec_send_packet",
    "av_codec_is_decoder",
}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: generate-forward-exports.py <original-libFFmpeg64.dll> <output-header>")
        return 2

    original_path = pathlib.Path(sys.argv[1])
    output_path = pathlib.Path(sys.argv[2])
    pe = pefile.PE(str(original_path))

    names: list[str] = []
    for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if symbol.name:
            names.append(symbol.name.decode("ascii"))
    names.sort()

    quote = '"'
    lines = [
        "#pragma once",
        "",
        "// Generated from the original Billfish libFFmpeg64.dll export table.",
        "// Hook exports are implemented by libFFmpeg64_av1_shim.cpp; all other exports",
        "// are forwarded to libFFmpeg64_orig.dll so the MediaSDK ABI stays unchanged.",
    ]
    for name in names:
        target = name if name in HOOK_EXPORTS else f"{name}=libFFmpeg64_orig.{name}"
        lines.append(f"#pragma comment(linker, {quote}/export:{target}{quote})")

    output_path.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"wrote {output_path} with {len(names)} exports, {len(HOOK_EXPORTS)} hooks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
