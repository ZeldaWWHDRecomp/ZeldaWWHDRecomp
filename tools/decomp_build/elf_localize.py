#!/usr/bin/env python3
"""`llvm-objcopy --localize-hidden OBJ` for the decomp version on Linux (tools/installer/decomp_platform.py): the pinned
zig toolchain setup uses there has `ld.lld -r` but no llvm-objcopy, and on ELF `ld -r` keeps hidden symbols global, so
the merged game unit's hidden symbols are made local here (cmake/Decomp.cmake does the same with llvm-objcopy on
Android). Units then never see each other's classes, inline helpers or gabi state.

usage: elf_localize.py OBJ [OUT]   (in place without OUT)

What changes, as llvm-objcopy does it (llvm/lib/ObjCopy/ELF):
  - every defined symbol (st_shndx not SHN_UNDEF) with STV_HIDDEN or STV_INTERNAL visibility gets STB_LOCAL binding
    (its visibility stays);
  - the symbol table is re-sorted with all local symbols first (a stable partition; ELF requires it) and sh_info is
    the first non-local index; every symbol index is renumbered: SHT_REL / SHT_RELA entries, SHT_GROUP signatures,
    SHT_SYMTAB_SHNDX, SHT_LLVM_ADDRSIG;
  - a group whose signature symbol became local loses GRP_COMDAT (it is the unit's own copy, never deduplicated
    against another unit's).
Only relocatable ELF64 objects (x86-64, AArch64; either byte order). Sections whose size changes (only
SHT_LLVM_ADDRSIG can: its indices are ULEB128) are written at the end of the file; the old bytes stay unused."""
import os
import struct
import sys

SHT_SYMTAB, SHT_RELA, SHT_REL, SHT_GROUP, SHT_SYMTAB_SHNDX = 2, 4, 9, 17, 18
SHT_CREL = 0x40000014
SHT_LLVM_ADDRSIG = 0x6FFF4C03
SHT_LLVM_CALL_GRAPH_PROFILE = 0x6FFF4C09  # (LLVM 13+: weights only, its symbols are in a SHT_REL section)
SHN_UNDEF, SHN_XINDEX = 0, 0xFFFF
STB_LOCAL = 0
STV_INTERNAL, STV_HIDDEN = 1, 2
GRP_COMDAT = 1
SHDR = "IIQQQQIIQQ"  # name type flags addr offset size link info addralign entsize
SYM = "IBBHQQ"       # name info other shndx value size


class ElfError(Exception):
    pass


def uleb(data, pos):
    v = shift = 0
    while True:
        b = data[pos]
        pos += 1
        v |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return v, pos


def put_uleb(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        out.append(b | (0x80 if v else 0))
        if not v:
            return bytes(out)


def localize_hidden(data):
    """data: the object's bytes -> (new bytes, number of symbols made local)"""
    data = bytearray(data)
    if data[:4] != b"\x7fELF":
        raise ElfError("not an ELF file")
    if data[4] != 2:
        raise ElfError("only 64-bit ELF objects are supported")
    e = "<" if data[5] == 1 else ">"
    e_type, = struct.unpack_from(e + "H", data, 16)
    if e_type != 1:
        raise ElfError("not a relocatable object (ET_REL)")
    e_shoff, = struct.unpack_from(e + "Q", data, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(e + "HHH", data, 0x3A)
    if e_shentsize != struct.calcsize(e + SHDR):
        raise ElfError("unexpected section header size %d" % e_shentsize)
    if e_shnum == 0 and e_shoff:  # (extended numbering: the count is in section 0's sh_size)
        e_shnum = struct.unpack_from(e + SHDR, data, e_shoff)[5]
    shdrs = [list(struct.unpack_from(e + SHDR, data, e_shoff + i * e_shentsize)) for i in range(e_shnum)]
    symtabs = [i for i, s in enumerate(shdrs) if s[1] == SHT_SYMTAB]
    if not symtabs:
        return bytes(data), 0
    if len(symtabs) != 1:
        raise ElfError("%d symbol tables" % len(symtabs))
    st = symtabs[0]
    _, _, _, _, off, size, _, first_global, _, ent = shdrs[st]
    if ent != struct.calcsize(e + SYM):
        raise ElfError("unexpected symbol size %d" % ent)
    syms = [list(x) for x in struct.iter_unpack(e + SYM, bytes(data[off:off + size]))]
    shndx_tab = next((i for i, s in enumerate(shdrs) if s[1] == SHT_SYMTAB_SHNDX and s[6] == st), None)
    xidx = None
    if shndx_tab is not None:
        o, n = shdrs[shndx_tab][4], shdrs[shndx_tab][5]
        xidx = list(struct.unpack_from(e + "%dI" % (n // 4), data, o))
    for i, s in enumerate(shdrs):  # every other section that refers to the symbol table must be one handled below
        if i != st and s[6] == st and s[1] not in (SHT_REL, SHT_RELA, SHT_GROUP, SHT_SYMTAB_SHNDX, SHT_LLVM_ADDRSIG,
                                                   SHT_LLVM_CALL_GRAPH_PROFILE):
            raise ElfError("section %d (type 0x%x) refers to the symbol table: not supported" % (i, s[1]))
        if s[1] == SHT_CREL:
            raise ElfError("CREL relocations are not supported")

    changed = 0
    for i in range(max(first_global, 1), len(syms)):
        s = syms[i]
        defined = s[3] != SHN_UNDEF and not (s[3] == SHN_XINDEX and xidx is not None and xidx[i] == SHN_UNDEF)
        if defined and (s[2] & 3) in (STV_HIDDEN, STV_INTERNAL) and s[1] >> 4 != STB_LOCAL:
            s[1] = (STB_LOCAL << 4) | (s[1] & 0xF)
            changed += 1
    if not changed:
        return bytes(data), 0
    # locals first, each group in its old order (index 0, the null symbol, stays first)
    order = [0] + [i for i in range(1, len(syms)) if syms[i][1] >> 4 == STB_LOCAL] + \
            [i for i in range(1, len(syms)) if syms[i][1] >> 4 != STB_LOCAL]
    new_of = [0] * len(syms)
    for n, o in enumerate(order):
        new_of[o] = n
    nlocal = sum(1 for i in order if syms[i][1] >> 4 == STB_LOCAL)  # (includes the null symbol)
    data[off:off + size] = b"".join(struct.pack(e + SYM, *syms[o]) for o in order)
    shdrs[st][7] = nlocal
    if xidx is not None:
        o = shdrs[shndx_tab][4]
        struct.pack_into(e + "%dI" % len(order), data, o, *[xidx[i] for i in order])

    appended = []
    for i, s in enumerate(shdrs):
        if s[6] != st or i == st:
            continue
        typ, o, n = s[1], s[4], s[5]
        if typ in (SHT_REL, SHT_RELA):
            esz = 16 if typ == SHT_REL else 24
            if s[9] and s[9] != esz:
                raise ElfError("unexpected relocation size %d" % s[9])
            for p in range(o, o + n, esz):
                info, = struct.unpack_from(e + "Q", data, p + 8)
                sym = info >> 32
                if sym:
                    struct.pack_into(e + "Q", data, p + 8, (new_of[sym] << 32) | (info & 0xFFFFFFFF))
        elif typ == SHT_GROUP:
            sig = s[7]
            s[7] = new_of[sig]
            if syms[sig][1] >> 4 == STB_LOCAL:
                flags, = struct.unpack_from(e + "I", data, o)
                struct.pack_into(e + "I", data, o, flags & ~GRP_COMDAT)
        elif typ == SHT_LLVM_ADDRSIG:
            out, p = bytearray(), o
            while p < o + n:
                v, p = uleb(data, p)
                out += put_uleb(new_of[v])
            if len(out) == n:
                data[o:o + n] = out
            else:
                appended.append((i, bytes(out)))
    for i, body in appended:
        while len(data) % 8:
            data.append(0)
        shdrs[i][4], shdrs[i][5] = len(data), len(body)
        data += body
    if appended:  # the section headers move behind the appended data
        while len(data) % 8:
            data.append(0)
        e_shoff = len(data)
        struct.pack_into(e + "Q", data, 0x28, e_shoff)
        data += b"\0" * (e_shnum * e_shentsize)
    for i, s in enumerate(shdrs):
        struct.pack_into(e + SHDR, data, e_shoff + i * e_shentsize, *s)
    return bytes(data), changed


def main(argv):
    if len(argv) not in (2, 3):
        sys.exit(__doc__.split("\n\n")[1])
    src = argv[1]
    dst = argv[2] if len(argv) == 3 else src
    with open(src, "rb") as f:
        data = f.read()
    try:
        out, _ = localize_hidden(data)
    except (ElfError, struct.error, IndexError) as ex:
        sys.exit("elf_localize.py: %s: %s" % (src, ex))
    tmp = dst + ".tmp"
    with open(tmp, "wb") as f:
        f.write(out)
    os.replace(tmp, dst)


if __name__ == "__main__":
    main(sys.argv)
