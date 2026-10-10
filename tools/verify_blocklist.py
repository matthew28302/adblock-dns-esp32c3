#!/usr/bin/env python3
"""Verify a built blocklist.bin before flashing:

1. ALLOW-CHECK: domains that must NEVER be blocked (social platforms etc.)
   must not appear in the blob.
2. BLOCK-CHECK: known ad domains (incl. curated social-ad ones) must be present.

Usage: python tools/verify_blocklist.py [blocklist.bin]
Exit code 0 = pass, 1 = fail.
"""
import sys, os, bisect

HASH_BYTES = 5
MASK = (1 << (HASH_BYTES * 8)) - 1
FNV_OFFSET = 0xcbf29ce484222325
FNV_PRIME = 0x100000001b3
U64 = (1 << 64) - 1

def fnv(b: bytes) -> int:
    h = FNV_OFFSET
    for c in b:
        h = ((h ^ c) * FNV_PRIME) & U64
    return h & MASK

# Mirrors the firmware's parseQuery() normalization (strip leading www.).
def norm(d: str) -> str:
    d = d.strip().lower().rstrip('.')
    return d[4:] if d.startswith('www.') else d

MUST_ALLOW = [
    'facebook.com', 'fb.com', 'fbcdn.net', 'instagram.com', 'cdninstagram.com',
    'whatsapp.com', 'whatsapp.net', 'messenger.com',
    'tiktok.com', 'tiktokcdn.com', 'tiktokcdn-us.com', 'tiktokv.com', 'byteoversea.net',
    'twitter.com', 'x.com', 'twimg.com',
    'youtube.com', 'googlevideo.com', 'ytimg.com', 'google.com', 'googleapis.com',
    'telegram.org', 't.me', 'discord.com',
    'zalo.vn', 'zaloapp.com', 'zing.vn', 'zingmp3.vn', 'vnexpress.net',
    'reddit.com', 'linkedin.com',
]
MUST_BLOCK = [
    'doubleclick.net', 'googleadservices.com', 'admob.com', 'app-measurement.com',
    'ads.tiktok.com', 'pangle.io', 'ads.pangle.io', 'an.facebook.com', 'ads.facebook.com',
    'applovin.com', 'chartboost.com', 'vungle.com', 'inmobi.com', 'mintegral.com',
    'unityads.unity3d.com', 'moloco.com', 'criteo.com', 'outbrain.com', 'taboola.com',
    'eclick.vn', 'admicro.vn', 'adtop.vn', 'zad.vn', 'advertising.zalo.vn',
]

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join('data', 'blocklist.bin')
    blob = open(path, 'rb').read()
    if len(blob) % HASH_BYTES:
        print(f'FAIL: size {len(blob)} not a multiple of {HASH_BYTES}')
        sys.exit(1)
    hashes = sorted(int.from_bytes(blob[i:i + HASH_BYTES], 'little')
                    for i in range(0, len(blob), HASH_BYTES))
    def contains(h):
        i = bisect.bisect_left(hashes, h)
        return i < len(hashes) and hashes[i] == h
    leaked = [d for d in MUST_ALLOW if contains(fnv(norm(d).encode()))]
    missing = [d for d in MUST_BLOCK if not contains(fnv(norm(d).encode()))]
    print(f'entries          : {len(hashes):,}')
    print(f'ALLOW-CHECK      : {"FAIL -> " + ", ".join(leaked) if leaked else "OK (no platform leaked into the list)"}')
    print(f'BLOCK-CHECK      : {"FAIL -> missing " + ", ".join(missing) if missing else "OK (all sample ad domains present)"}')
    sys.exit(1 if (leaked or missing) else 0)

if __name__ == '__main__':
    main()
