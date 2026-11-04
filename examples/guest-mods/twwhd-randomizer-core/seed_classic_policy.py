"""Classify the complete classic-mode family using whole-function hashes.

Catalogued from official randomizer outputs. No original instruction bytes or
small recoverable hashes are shipped. All seven source consumers are selected together by the validated flag.
"""
import hashlib

FUNCTIONS = (
    (0x02190CE0, 0x02193770,
     '05261952293472b642a49c0fa09496fedafb4fde33f6ee5f7b49c87335872bce',
     '07072fe55450e0bdfb3cb52c2c8d3e6900fcc5ff66494185f01f9368a2e5b852'),
    (0x022F7D68, 0x022F8108,
     'c853b127586ff1613fb7ce2e11d9b39f321401421bf16b97eb9800510e486281',
     'e7572c8827a7ed18e0f2891b0aaaacaded5c444f3f527e9a051808a1f8ba17ab'),
    (0x023F2048, 0x023F22FC,
     '29b6370ad4ad61824a7244f735c511f164e8d5f5d27ef5bb6d02474feff363c1',
     '85df7bff0611f8b1605c2caba1521e9f5cc60f1be6348f5de7d9196eb14973b6'),
    (0x023FEA6C, 0x023FF020,
     '9cfb6d75ba415ee51cbd565a4460ea0bd15ac7e886077b821fc94336bb3b56e1',
     '85b96efb4e7fca2f1dcc8c499f89f36d8b65546b43f0e79b6af9ff5e2b566aa1'),
    (0x0243A094, 0x0243AAD4,
     '899803c3d66d66277482766ea14d2811c1b1d16bb6f2810c702126bd130c7cc9',
     '8ea38249b242c6baa50b6f77b6bcf5094289e967a0464a07bc5dc4044f73661d'),
    (0x0270D70C, 0x0270DB88,
     '66e783357f9b8487fcfac4ff28875487b4db7dc06f0fd8ae040cca28300c46ea',
     '1f2e3724deb41ecc7b0c6f9567b744adacc95be7fda0c9370486c523059ab871'),
    (0x0270F01C, 0x0270F210,
     'f750029f781766076389ecf232bb126ab1555387152c30089f3f867346dc8c46',
     '74783cbefe2104cbcc874a956f0e43a9c6cd82fa504c453301cefc185cb48443'),
 )


def extract_classic_policy(read):
    states = []
    for start, end, disabled, enabled in FUNCTIONS:
        body = read(start, end-start)
        if not isinstance(body, (bytes, bytearray, memoryview)) or len(body) != end-start:
            raise ValueError('truncated classic-mode function')
        digest = hashlib.sha256(body).hexdigest()
        if digest == disabled:
            states.append(0)
        elif digest == enabled:
            states.append(1)
        else:
            raise ValueError(f'unsupported classic-mode function at {start:08X}')
    if len(set(states)) != 1:
        raise ValueError('incoherent classic-mode functions')
    return states[0]
