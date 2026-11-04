"""Decode the coherent optional music policy from full reviewed function hashes.

No game instruction bytes or tiny recoverable hashes are retained. The six
function ranges are canonical original-USA boundaries; ON hashes represent only
MIT upstream remove_music.asm's complete early-return policy.
"""
import hashlib

FUNCTIONS = (
    (0x0202204C, 0x020233C8,
     '78076a613a4363154462ea06a95c0ee0219b763ee4f224e5d70c6f5a154bb647',
     'a453639011107fea018c72b4ceef66ff33437897253b34d38c6fd9ded40a9e42'),
    (0x0202879C, 0x02028D6C,
     '74c970ebd664aa90389fbace5076aa054bdbeb1e95258b604c34c18f0b5ad462',
     'b53f8164299a7e611e372859062f72614fefafee023c51232a256a1fd1cf18f1'),
    (0x0201DE00, 0x0201E480,
     '9cbe65837e2568c570965bd1ddbcc296c8829456e411841f7022a6ccb18d9140',
     '7fe351f7fe8fd1028c301b341438651c46df4a2f85bf0f8e9258cb38a1d80723'),
    (0x02027F04, 0x02027FD4,
     'b0c764263438ec7aca6c4c16c04649302664eba842be348801905f00dd76564f',
     'b40ea3031b7702259bb183cb555e59b1b8272d7de49fe24fc3550dd0e7f23708'),
    (0x02897F48, 0x02897FD8,
     '8061eba68157c72d91924d2e60a5d0e47b611ce4154a1e6199371481c0a0c9a5',
     'fb72341496f12c1328ef615c0fecebc961f14af4ce04c99a58f9f156b4feb024'),
    (0x02030F1C, 0x02030FA8,
     '8c77ce514462f8d859975de938ece7ca2cc54df7802b66628633f6e3d9206619',
     '316682d4e172e7c3ec40a156bfc46c05c655e830388d0283a9ee0377c239fd92'),
)


def extract_music_policy(read):
    states = []
    for start, end, disabled, enabled in FUNCTIONS:
        body = read(start, end-start)
        if not isinstance(body, (bytes, bytearray, memoryview)) or len(body) != end-start:
            raise ValueError(f'truncated optional music function at {start:08X}')
        digest = hashlib.sha256(body).hexdigest()
        if digest == disabled:
            states.append(0)
        elif digest == enabled:
            states.append(1)
        else:
            raise ValueError(f'unsupported optional music function at {start:08X}')
    if len(set(states)) != 1:
        raise ValueError('incoherent optional music policy; all six functions must agree')
    return states[0]
