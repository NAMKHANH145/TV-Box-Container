#!/usr/bin/env python3
"""image service: cai Pillow (co .so native) + xu ly anh + do CPU brand."""
import hashlib
import json
import os
import platform
import struct
import time

from PIL import Image, ImageDraw


def elf_machine(path="/proc/self/exe"):
    with open(path, "rb") as fh:
        hdr = fh.read(20)
    return struct.unpack_from("<H", hdr, 18)[0] if hdr[:4] == b"\x7fELF" else None


# --- CPU-bound de phat hien chay gia lap (qemu) ---
t0 = time.perf_counter()
n = 200000
sieve = bytearray(b"\x01") * (n + 1)
sieve[:2] = b"\x00\x00"
for i in range(2, int(n ** 0.5) + 1):
    if sieve[i]:
        sieve[i * i::i] = bytearray(len(sieve[i * i::i]))
cpu_ms = (time.perf_counter() - t0) * 1000

# --- xu ly anh that bang Pillow (file .so native) ---
img = Image.new("RGB", (64, 64), (20, 40, 80))
ImageDraw.Draw(img).rectangle((8, 8, 55, 55), fill=(220, 180, 40))
small = img.resize((16, 16))
out = "/tmp/mini.png"
small.save(out)
png_sha = hashlib.sha256(open(out, "rb").read()).hexdigest()[:16]

print(json.dumps({
    "program": "app.py (pip install Pillow - normal workflow)",
    "pillow_version": __import__("PIL").__version__,
    "pillow_path": os.path.dirname(Image.__file__),
    "elf_e_machine": elf_machine(),
    "platform_machine": platform.machine(),
    "kernel": open("/proc/sys/kernel/osrelease").read().strip(),
    "prime_count": sum(sieve),
    "cpu_ms": round(cpu_ms, 1),
    "png_sha256_16": png_sha,
    "pid": os.getpid(),
}, sort_keys=True))
