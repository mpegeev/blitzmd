#!/usr/bin/env python3
"""Generates src/twemoji-flags.bin, the flag emoji set embedded in the plugin.

Usage:
    python scripts/gen_flags.py [--svg-dir DIR] [--tag TAG] [-o OUTPUT]

Windows' emoji font has no country flags, so BlitzMD draws them from Twemoji
(https://github.com/jdecked/twemoji, graphics CC-BY 4.0): every regional
indicator pair (1f1fa-1f1f8.svg = US) and every subdivision flag
(1f3f4-e0067-e0062-e0065-e006e-e0067-e007f.svg = England). The SVGs come
from DIR (for example assets/svg of a Twemoji checkout) or are downloaded
for release TAG.

Coordinates are cut to two decimals (a flag is 36 units wide: at most 1/3600
of its size; only digits after a point go, so compact path data such as the
arc flags in "a1 1 0 01.5.5" stays intact) and the SVGs are packed as

    "MDVF", uint32 count,
    count x { char key[8]; uint32 offset; uint32 length; }   sorted by key
    uint32 packed size, MSZIP data (Windows Compression API, buffer mode)

where key is the lowercase region code ("us", "gbeng") and offset/length
address the SVG in the decompressed data. The index stays uncompressed so
the plugin knows which flags exist without decompressing anything.

Needs Windows (cabinet.dll) and only the Python standard library. The output
is deterministic.
"""

import argparse
import ctypes
import json
import pathlib
import re
import struct
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from ctypes import wintypes

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPO_ROOT / "src" / "twemoji-flags.bin"
DEFAULT_TAG = "v17.0.3"
REPO = "jdecked/twemoji"

RI = r"1f1(?:e[6-9a-f]|f[0-9a-f])"
PAIR_RE = re.compile(rf"({RI})-({RI})\.svg")
TAGS_RE = re.compile(r"1f3f4((?:-e00[2-7][0-9a-f])+)-e007f\.svg")
EXTRA_DIGITS_RE = re.compile(rb"(\.\d\d)\d+")
GEOMETRY_RE = re.compile(rb'( (?:d|transform|cx|cy|r|rx|ry)=")([^"]*)(")')

COMPRESS_ALGORITHM_MSZIP = 2


def flag_key(name: str) -> str | None:
    m = PAIR_RE.fullmatch(name)
    if m:
        return "".join(chr(ord("a") + int(cp, 16) - 0x1F1E6) for cp in m.groups())
    m = TAGS_RE.fullmatch(name)
    if m:
        return "".join(chr(int(cp, 16) - 0xE0000) for cp in m.group(1).split("-")[1:])
    return None


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "gen_flags.py"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read()


def download(tag: str) -> dict[str, bytes]:
    tree = json.loads(fetch(f"https://api.github.com/repos/{REPO}/git/trees/{tag}?recursive=1"))
    names = [e["path"][len("assets/svg/"):] for e in tree["tree"] if e["path"].startswith("assets/svg/")]
    names = [n for n in names if flag_key(n)]
    base = f"https://raw.githubusercontent.com/{REPO}/{tag}/assets/svg/"
    with ThreadPoolExecutor(16) as pool:
        return dict(zip(names, pool.map(lambda n: fetch(base + n), names)))


def shrink(svg: bytes) -> bytes:
    return GEOMETRY_RE.sub(lambda m: m.group(1) + EXTRA_DIGITS_RE.sub(rb"\1", m.group(2)) + m.group(3), svg)


def mszip(data: bytes, decompress: bool = False, size: int = 0) -> bytes:
    cab = ctypes.WinDLL("cabinet")
    create = cab.CreateDecompressor if decompress else cab.CreateCompressor
    run = cab.Decompress if decompress else cab.Compress
    close = cab.CloseDecompressor if decompress else cab.CloseCompressor
    create.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    run.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                    ctypes.POINTER(ctypes.c_size_t)]
    close.argtypes = [ctypes.c_void_p]
    handle = ctypes.c_void_p()
    if not create(COMPRESS_ALGORITHM_MSZIP, None, ctypes.byref(handle)):
        sys.exit("cabinet.dll: cannot create the (de)compressor")
    out = ctypes.create_string_buffer(size if decompress else len(data) + 65536)
    n = ctypes.c_size_t()
    ok = run(handle, data, len(data), out, len(out), ctypes.byref(n))
    close(handle)
    if not ok:
        sys.exit("cabinet.dll: (de)compression failed")
    return out.raw[: n.value]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--svg-dir", type=pathlib.Path, help="Twemoji assets/svg directory (default: download)")
    ap.add_argument("--tag", default=DEFAULT_TAG, help=f"Twemoji release to download (default {DEFAULT_TAG})")
    ap.add_argument("-o", "--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    args = ap.parse_args()

    if args.svg_dir:
        files = {p.name: p.read_bytes() for p in args.svg_dir.glob("*.svg") if flag_key(p.name)}
    else:
        files = download(args.tag)
    flags = sorted((flag_key(name), shrink(svg)) for name, svg in files.items())
    if len(flags) < 250:
        sys.exit(f"only {len(flags)} flags found")

    data = bytearray()
    index = bytearray()
    for key, svg in flags:
        if len(key) > 8:
            sys.exit(f"key too long: {key}")
        index += struct.pack("<8sII", key.encode(), len(data), len(svg))
        data += svg
    packed = mszip(bytes(data))
    if mszip(packed, decompress=True, size=len(data)) != data:
        sys.exit("round trip failed")
    blob = b"MDVF" + struct.pack("<I", len(flags)) + index + struct.pack("<I", len(packed)) + packed
    args.output.write_bytes(blob)
    print(f"{len(flags)} flags, {len(data)} bytes of SVG -> {len(blob)} bytes: {args.output}")


if __name__ == "__main__":
    main()
