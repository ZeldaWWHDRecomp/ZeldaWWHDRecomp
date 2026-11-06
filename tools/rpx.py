"""Wii U RPX/RPL container: sections, symbols, relocations."""
import hashlib
import os
import struct
import zlib

SHT_SYMTAB, SHT_RELA, SHT_NOBITS = 2, 4, 8
SHT_RPL_IMPORTS = 0x80000002
SHF_RPL_ZLIB = 0x08000000

R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA, R_PPC_REL24 = 1, 4, 5, 6, 10


class Section:
    pass


class Symbol:
    pass


class Rpx:
    def __init__(self, path, limits=None):
        # Only untrusted randomized inputs opt in; immutable retail builds keep
        # their existing parser behavior. Bound the read as well as the stat.
        if limits and os.path.getsize(path) > limits['file']:
            raise ValueError("RPX input exceeds size limit")
        with open(path, "rb") as stream:
            d = stream.read(limits['file']+1) if limits else stream.read()
        if limits and len(d) > limits['file']:
            raise ValueError("RPX input exceeds size limit")
        if limits and (len(d) < 0x34 or d[:6] != b"\x7fELF\x01\x02" or
                       struct.unpack_from(">H", d, 18)[0] != 20):
            raise ValueError("RPX input is not a 32-bit big-endian PowerPC RPX")
        self.buffer_sha256 = hashlib.sha256(d).hexdigest() if limits else None
        total = 0
        self.entry, = struct.unpack_from(">I", d, 0x18)
        shoff, = struct.unpack_from(">I", d, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from(">HHH", d, 0x2E)
        if limits and shnum > limits.get('sections', shnum):
            raise ValueError("RPX section count exceeds limit")
        self.limits = limits
        self._name_bytes = 0
        self.sections = []
        for i in range(shnum):
            s = Section()
            (s.name_off, s.type, s.flags, s.addr, s.offset, s.file_size,
             s.link, s.info, s.align, s.entsize) = struct.unpack_from(">10I", d, shoff + i * shentsize)
            s.index = i
            if limits and (s.file_size > limits['section'] or
                           s.type != SHT_NOBITS and s.offset+s.file_size > len(d)):
                raise ValueError("RPX section exceeds input or size limit")
            if limits and s.flags & SHF_RPL_ZLIB and s.file_size < 4:
                raise ValueError("RPX compressed section is truncated")
            if s.type == SHT_NOBITS or s.file_size == 0:
                s.data = b""
                s.size = s.file_size
            else:
                raw = d[s.offset:s.offset + s.file_size]
                if s.flags & SHF_RPL_ZLIB:
                    if limits:
                        if len(raw) < 4:
                            raise ValueError("RPX compressed section is truncated")
                        declared, = struct.unpack_from(">I", raw)
                        budget = min(limits['section'], limits['inflated']-total)
                        if declared > budget:
                            raise ValueError("RPX inflated section exceeds size limit")
                        inflater = zlib.decompressobj()
                        raw = inflater.decompress(raw[4:], declared+1)
                        if (len(raw) != declared or not inflater.eof or
                                inflater.unconsumed_tail or inflater.unused_data):
                            raise ValueError("RPX compressed section length or stream is invalid")
                    else:
                        raw = zlib.decompress(raw[4:])
                s.data = raw
                s.size = len(raw)
            total += s.size
            if limits and (s.size > limits['section'] or total > limits['inflated']):
                raise ValueError("RPX aggregate inflated size exceeds limit")
            self.sections.append(s)
        shstr = self.sections[shstrndx].data
        for s in self.sections:
            s.name = self._read_name(shstr, s.name_off)
        self.by_name = {s.name: s for s in self.sections}
        self._load_symbols()
        self._load_relocs()

    def _read_name(self, buf, off):
        if not self.limits:
            return _cstr(buf, off)
        # Search a bounded span, then charge the raw UTF-8 bytes before slicing
        # or decoding. Repeated/overlapping offsets cannot amplify a tiny table
        # into unbounded Python strings. Section and symbol names share a budget.
        remaining = self.limits.get('name_bytes', 1 << 20)-self._name_bytes
        maximum = min(self.limits.get('name', 1024), remaining)
        if off < 0 or off >= len(buf):
            raise ValueError("RPX name offset is invalid")
        end = buf.find(b"\0", off, off+maximum+1)
        if end < 0:
            raise ValueError("RPX name length or aggregate name budget exceeds limit")
        self._name_bytes += end-off
        return buf[off:end].decode("utf-8", "replace")

    def _load_symbols(self):
        self.symbols = []
        count = sum(len(s.data)//16 for s in self.sections if s.type == SHT_SYMTAB)
        if self.limits and count > self.limits.get('symbols', count):
            raise ValueError("RPX symbol count exceeds limit")
        for s in self.sections:
            if s.type != SHT_SYMTAB:
                continue
            strtab = self.sections[s.link].data
            for j in range(len(s.data) // 16):
                sym = Symbol()
                name, sym.value, sym.size, info, _other, sym.shndx = struct.unpack_from(">IIIBBH", s.data, j * 16)
                sym.name = self._read_name(strtab, name)
                sym.type, sym.bind = info & 0xF, info >> 4
                sym.section = self.sections[sym.shndx] if 0 < sym.shndx < len(self.sections) else None
                sym.import_lib = None
                if sym.section is not None and sym.section.type == SHT_RPL_IMPORTS:
                    sym.import_lib = sym.section.name.split("_", 1)[1]
                    sym.import_kind = sym.section.name[1]  # 'f' function, 'd' data
                self.symbols.append(sym)

    def _load_relocs(self):
        """list of (target section, offset-address, type, symbol, addend)"""
        self.relocs = []
        count = sum(len(s.data)//12 for s in self.sections if s.type == SHT_RELA)
        if self.limits and count > self.limits.get('relocations', count):
            raise ValueError("RPX relocation count exceeds limit")
        for s in self.sections:
            if s.type != SHT_RELA:
                continue
            target = self.sections[s.info]
            for k in range(len(s.data) // 12):
                off, info, addend = struct.unpack_from(">IIi", s.data, k * 12)
                self.relocs.append((target, off, info & 0xFF, self.symbols[info >> 8], addend))


def _cstr(buf, off):
    return buf[off:buf.index(b"\0", off)].decode("utf-8", "replace")
