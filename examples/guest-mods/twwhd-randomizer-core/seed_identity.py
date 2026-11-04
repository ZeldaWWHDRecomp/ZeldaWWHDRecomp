"""Bind a prepared source-mod seed to both code-derived data and content."""
import hashlib


def seed_identity(rpx_sha256, content_sha256):
    for value in (rpx_sha256, content_sha256):
        if not isinstance(value, str) or len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
            raise ValueError('seed identity requires lowercase SHA-256 input hashes')
    return hashlib.sha256(b'WWHD source-mod seed v1\0' + bytes.fromhex(rpx_sha256) +
                          bytes.fromhex(content_sha256)).hexdigest()
