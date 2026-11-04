"""Validate the mandatory USA permanent-resource allocation consumer.

The complete 2,116-byte reviewed randomized function is hashed. No executable
bytes or small recoverable hashes are retained. Ordinary executable setup is
unaffected; source-core preparation must call this validator before output.
"""
import hashlib

START, END = 0x0203EE2C, 0x0203F670
EXPECTED = '370a685dd83ed11011223882e3b744457b322a3dd9055a7585b01689b977302a'


def validate_permanent_resource_policy(read):
    body = read(START, END-START)
    if not isinstance(body, (bytes, bytearray, memoryview)) or len(body) != END-START:
        raise ValueError('truncated permanent-resource heap consumer')
    if hashlib.sha256(body).hexdigest() != EXPECTED:
        raise ValueError('unsupported permanent-resource heap consumer')
