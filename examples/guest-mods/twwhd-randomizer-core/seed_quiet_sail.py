"""Reviewed complete procSteerMove variants; no original game bytes ship here.

The ON variant differs only at MIT upstream quiet_swift_sail.asm's three
sound calls. Hash the entire 1,684-byte function, never recoverable tiny words.
"""
import hashlib

START, END = 0x0247E854, 0x0247EEE8
DISABLED = 'cd01ffa180b939c4d0fc772536c748b9f3ca16f41c7b70e4669e760dc39f5877'
ENABLED = '993968e5c212e9f7374bfc197730518e66ca3ae07127d048b378d931b980c7b4'


def extract_quiet_sail(read):
    body = read(START, END-START)
    if not isinstance(body, (bytes, bytearray, memoryview)) or len(body) != END-START:
        raise ValueError('truncated quiet Swift Sail function')
    digest = hashlib.sha256(body).hexdigest()
    if digest == DISABLED:
        return 0
    if digest == ENABLED:
        return 1
    raise ValueError('unsupported quiet Swift Sail function')
