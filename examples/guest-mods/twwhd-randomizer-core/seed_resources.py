"""Decode data-only item resource changes from separately validated USA images.

Readers must enforce section bounds. These fixed tables contain resource metadata,
not game instructions. RPX identity/profile validation remains the caller's job.
"""
TABLES = {'field': (0x101E6A74, 28, 4), 'item': (0x101E4674, 36, 8)}
INFO_BASE = 0x101E8674
ARCHIVE_CHARACTERS = frozenset(b'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_')


def extract_resources(original_read, seed_read):
    def exact(reader, address, size):
        data = reader(address, size)
        if len(data) != size:
            raise ValueError('short item resource data')
        return data

    def archive(address):
        if not 0x10000000 <= address < 0x10400000:
            raise ValueError('unsupported item archive pointer')
        data = exact(seed_read, address, 64)
        if b'\0' not in data:
            raise ValueError('unterminated item archive name')
        name = data.split(b'\0', 1)[0]
        if not name or any(byte not in ARCHIVE_CHARACTERS for byte in name):
            raise ValueError('unsupported item archive name')
        return name.decode('ascii')

    rows = []
    for table, (base, stride, offset) in TABLES.items():
        for item in range(256):
            before = exact(original_read, base+item*stride, stride)
            after = exact(seed_read, base+item*stride, stride)
            if before == after:
                continue
            if table == 'item' and before[4:8] != after[4:8]:
                raise ValueError('unsupported item texture pointer change')
            pointer = int.from_bytes(after[:4], 'big')
            if not pointer and before[:4] != after[:4]:
                raise ValueError('unsupported cleared item archive pointer')
            rows.append({'table': table, 'id': item, 'archive': archive(pointer) if pointer else None,
                         'payload': list(after[offset:])})
    before = exact(original_read, INFO_BASE, 1024)
    after = exact(seed_read, INFO_BASE, 1024)
    return {'rows': rows, 'info_delta': [(index, value) for index, value in enumerate(after) if value != before[index]]}


def render_resources(data):
    import json
    lines = ['/* Private player-owned item resource data. */']
    for row in data['rows']:
        table, item = row['table'], row['id']
        if table not in TABLES or type(item) is not int or not 0 <= item < 256:
            raise ValueError('invalid resource descriptor')
        _, stride, offset = TABLES[table]
        name, payload = row['archive'], row['payload']
        if name is not None:
            if not isinstance(name, str) or not 1 <= len(name) < 64 or any(ord(c) not in ARCHIVE_CHARACTERS for c in name):
                raise ValueError('invalid resource archive name')
            lines.append(f'static const char resource_{table}_{item}_arc[]={json.dumps(name)};')
        if len(payload) != stride-offset or any(type(value) is not int or not 0 <= value < 256 for value in payload):
            raise ValueError('invalid resource payload')
        lines.append(f'static const u8 resource_{table}_{item}_payload[]={{' + ','.join(map(str, payload)) + '};')
    lines.append('static void randomizer_apply_item_resources(void) {')
    for row in data['rows']:
        table, item = row['table'], row['id']; base, stride, offset = TABLES[table]
        lines.append(f' {{ volatile u8* dest=(volatile u8*)&WWHD_GAME_DATA(0x{base:08X},u8)+{item*stride};')
        if row['archive'] is not None:
            lines.append(f' *(volatile u32*)dest=(u32)resource_{table}_{item}_arc;')
        lines.append(f' for(u32 n=0;n<sizeof(resource_{table}_{item}_payload);n++) dest[n+{offset}]=resource_{table}_{item}_payload[n]; }}')
    for index, value in data['info_delta']:
        if type(index) is not int or not 0 <= index < 1024 or type(value) is not int or not 0 <= value < 256:
            raise ValueError('invalid item info delta')
        lines.append(f' *((volatile u8*)&WWHD_GAME_DATA(0x{INFO_BASE:08X},u8)+{index})={value};')
    lines.append('}')
    return '\n'.join(lines)+'\n'
