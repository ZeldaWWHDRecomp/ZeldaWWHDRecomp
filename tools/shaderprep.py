#!/usr/bin/env python3
"""Extract the game's GX2 shaders and build a shader "head start" for the runtime cache.

Usage:
  shaderprep.py scan     [--game DIR]                           list containers and shader counts
  shaderprep.py coverage RUNTIME_CACHE                           runtime programs found in the game files
  shaderprep.py template RUNTIME_CACHE [OUT] [--merge T]         distill one recorded runtime cache (shaders.bin
                                                                 or a Vulkan *.recipes file) into the state template (default OUT:
                                                                 tools/shadercache/template.bin);
                                                                 --merge keeps what template T already has
  shaderprep.py export   [OUT] [--from DIR]                    players: your own shader caches -> a code-free
                                                                 template to attach to issue #177 (default OUT:
                                                                 my-template.bin in the release folder); finds
                                                                 the caches itself, --from names their folder
  shaderprep.py merge    OUT T1 [T2 ...]                         maintainers: merge code-free templates
  shaderprep.py stage-template OUT [CACHE] [--from DIR] [--base T]
                                                                 mod makers: the render states your shader caches
                                                                 (found as for export, or CACHE) have beyond
                                                                 template T (default: the release's) -> a code-free
                                                                 template a mod package ships with its stages
                                                                 (docs/mod-stages.md, "Shaders")
  shaderprep.py build    [OUT] [--template T] [--max N]          head start from the game files + template
                         [--no-pipelines] [--jobs N]             (default OUT: game/shadercache/headstart.bin);
                         [--progress]                            --jobs: archive scan workers (default: a few);
                                                                 --progress: "WWHD-PROGRESS done total detail"
                                                                 lines on stdout

Where the shaders are (Wind Waker HD, USA):
  - SHARCFB archives ("BAHS" = byte-swapped "SHAB", AGL binary shader archive, version 9, little-endian
    header): standalone in content/Common/Shaders/{Prim,particle,render_buffer}/*.sharcfb, inside the AGL
    resource SARCs (content/Cafe/Common/agl_resource_cafe*.sarc) and inside many Yaz0-compressed SARC
    archives (*.szs, and the *.pack SARCs that bundle szs files) - e.g. model.sharcfb (8976 shaders),
    agl_technique_{pfx,lght,shdw}.sharcfb, wii_pipeline.sharcfb and per-stage archives (zl, pz, vf_*...).
    About 30,000 distinct shaders in total.
  - SHARCFB layout (kinnay/Nintendo-File-Formats documents v8; v9 matches for what we use):
      header: magic, version, filesize, endianness, 0, name length, name
      binary section: section size, count, then entries {entry size, type (0 VS, 1 PS, 2 GS), unknown,
        data size, data[]}. The data is the GX2VertexShader / GX2PixelShader structure stored
        little-endian, pointers as offsets from the start of the data. The microcode is stored
        byte-for-byte as the game later hands it to the GPU (95% of the programs the game uses from boot
        to Outset Island are found this way).
      program section: named programs referencing binaries (variations = macro combinations).
  - GX2VertexShader: 52 register words (0x00..0xCC), shader size @0xD0, shader offset @0xD4.
    GX2PixelShader: 41 register words (0x00..0xA0), shader size @0xA4, shader offset @0xA8.
  - .gsh (GFX2) files only hold the AGL primitive renderer's few shaders; not used by this tool.

The head start is written in the runtime's recipe format (runtime/src/gfx/metal_draw.mm, cache_write):
records of {u32 type, u32 raw size, u32 compressed size, zlib(raw)}; a shader record is
{u32 vertex, u32 size, u32 fsSize, u32 nregs, microcode, fetch microcode, (u32 reg, u32 value) * nregs};
type 2 is the Metal PipelineRecipe struct (168 bytes). Type 4 is a checked,
versioned Vulkan database containing exact keys, SPIR-V, descriptors and fixed
state, plus the local device/driver/features fingerprint. Handles are never stored.

The distributable template is WWHT0003 + bounded zlib canonical JSON. Strict
fields contain only program hashes/stages, register pairs, semantic fetch
attributes (buffer/location/format/numeric/signed/endian/component selectors,
offset/fetch type/signed divisor), and explicitly typed pipeline state. No
program bytes, fetch instructions, shader source, SPIR-V or native cache data
are accepted. See shaderprep_format.py for the versioned schema. Legacy local
templates can be converted using template --merge; unsupported fetch features
fail closed. Build resolves program hashes from the player's own archives and
writes a LOCAL code-carrying headstart. Never distribute that generated file.
"""
import os
import sys
# Windows' embeddable Python (the release's tools\python) runs isolated: it does not add the script's
# folder to sys.path, so the sibling module is found explicitly.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shaderprep_format as template_format  # noqa: E402
import json
import collections
import ctypes
import glob
import hashlib
import shlex
import shutil
import struct
import subprocess
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TEMPLATE = os.path.join(HERE, "shadercache", "template.bin")  # guarded code-free release template

REC_SHADER, REC_PIPELINE, REC_SPECULATIVE = 1, 2, 3
REC_VULKAN_PIPELINE = 4
VS, PS, GS = 0, 1, 2


# ---------------------------------------------------------------- containers
_yaz0 = None


def yaz0_python(b):
    """Portable fallback when no local C compiler is available."""
    if len(b) < 16 or b[:4] != b"Yaz0":
        return b""
    n = struct.unpack_from(">I", b, 4)[0]
    if n > 256 * 1024 * 1024:
        return b""
    out, at = bytearray(), 16
    try:
        while len(out) < n:
            code = b[at]
            at += 1
            for bit in range(7, -1, -1):
                if len(out) >= n:
                    break
                if code & (1 << bit):
                    out.append(b[at])
                    at += 1
                else:
                    first, second = b[at], b[at + 1]
                    at += 2
                    distance = ((first & 15) << 8 | second) + 1
                    length = first >> 4
                    if not length:
                        length = b[at] + 18
                        at += 1
                    else:
                        length += 2
                    if distance > len(out):
                        return b""
                    for _ in range(min(length, n - len(out))):
                        out.append(out[-distance])
    except IndexError:
        return b""
    return bytes(out)


YAZ0_SUFFIX = ".dll" if os.name == "nt" else ".dylib" if sys.platform == "darwin" else ".so"
# Release packages ship the decoder prebuilt (CMake target yaz0, tools/release/package.py);
# a source checkout builds it on demand into build/.
PACKAGED_YAZ0 = os.path.join(HERE, "lib", "libyaz0" + YAZ0_SUFFIX)


def android_library_dir(maps_path="/proc/self/maps"):
    """Android: the app's nativeLibraryDir, where the APK's libyaz0.so is installed next to the
    embedded libpython; None elsewhere or when it cannot be found."""
    if not (sys.platform == "android" or hasattr(sys, "getandroidapilevel")):
        return None
    try:
        with open(maps_path) as maps:
            for line in maps:
                path = line.split(None, 5)[-1].strip() if line.count(" ") >= 5 else ""
                if os.path.basename(path).startswith("libpython") and path.endswith(".so"):
                    return os.path.dirname(path)
    except OSError:
        pass
    return None


def yaz0_candidates(environ=None, android_dir=None):
    """Prebuilt decoders to try, in order: WWHD_YAZ0_LIB (Android setup passes the APK's copy),
    the app's nativeLibraryDir on Android, then a release package's tools/lib."""
    environ = os.environ if environ is None else environ
    found = []
    if environ.get("WWHD_YAZ0_LIB"):
        found.append(environ["WWHD_YAZ0_LIB"])
    android_dir = android_library_dir() if android_dir is None else android_dir
    if android_dir:
        found.append(os.path.join(android_dir, "libyaz0.so"))
    found.append(PACKAGED_YAZ0)
    return [path for i, path in enumerate(found) if path not in found[:i] and os.path.isfile(path)]


def _load_yaz0():
    source = os.path.join(HERE, "yaz0.c")
    prebuilt = yaz0_candidates()
    if prebuilt and (not os.path.isfile(source) or prebuilt[0] != PACKAGED_YAZ0):
        for path in prebuilt:
            try:
                return ctypes.CDLL(path)
            except OSError as error:
                print("shaderprep: cannot load %s (%s)" % (path, error), file=sys.stderr)
    lib = os.path.join(ROOT, "build", "libyaz0" + YAZ0_SUFFIX)
    try:
        if not os.path.exists(lib) or os.path.getmtime(lib) < os.path.getmtime(source):
            os.makedirs(os.path.dirname(lib), exist_ok=True)
            compiler = shlex.split(os.environ["CC"]) if os.environ.get("CC") else [shutil.which("clang") or shutil.which("gcc") or "clang"]
            subprocess.check_call(compiler + ["-O2", "-shared"] + ([] if os.name == "nt" else ["-fPIC"]) + ["-o", lib, source])
        return ctypes.CDLL(lib)
    except (OSError, subprocess.CalledProcessError):
        if os.path.isfile(PACKAGED_YAZ0):
            return ctypes.CDLL(PACKAGED_YAZ0)
        raise


def yaz0(b):
    global _yaz0
    if _yaz0 is None:
        try:
            _yaz0 = _load_yaz0()
            _yaz0.yaz0_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
            _yaz0.yaz0_decompress.restype = ctypes.c_long
        except (OSError, subprocess.CalledProcessError):
            print("shaderprep: native Yaz0 helper unavailable; using portable Python decoder", file=sys.stderr)
            _yaz0 = False
    if _yaz0 is False:
        return yaz0_python(b)
    if len(b) < 16 or b[:4] != b"Yaz0":
        return b""
    n = struct.unpack_from(">I", b, 4)[0]
    if n > 256 * 1024 * 1024:
        return b""
    out = ctypes.create_string_buffer(n)
    r = _yaz0.yaz0_decompress(b, len(b), out, n)
    return out.raw[:r] if r == n else b""


def sarc(b):
    e = ">" if b[6:8] == b"\xfe\xff" else "<"
    hl = struct.unpack_from(e + "H", b, 4)[0]
    doff = struct.unpack_from(e + "I", b, 0xC)[0]
    n = struct.unpack_from(e + "H", b, hl + 6)[0]
    sfnt = hl + 12 + 16 * n
    for i in range(n):
        h, attr, s, t = struct.unpack_from(e + "IIII", b, hl + 12 + 16 * i)
        name = "%08x" % h
        if attr & 0x01000000:
            off = sfnt + 8 + (attr & 0xFFFF) * 4
            name = b[off:b.index(b"\0", off)].decode(errors="replace")
        yield name, b[doff + s:doff + t]


def walk(b, path):
    """yield (path, bytes) for every file inside Yaz0/SARC nesting"""
    if b[:4] == b"Yaz0":
        yield from walk(yaz0(b), path)
    elif b[:4] == b"SARC":
        for n, f in sarc(b):
            yield from walk(f, path + "/" + n)
    else:
        yield path, b


def sharcfb_shaders(b):
    """yield (type, register words, microcode) for every vertex/pixel shader binary"""
    if b[:4] != b"BAHS":
        return
    u = lambda o: struct.unpack_from("<I", b, o)[0]
    o = 0x18 + u(0x14)
    count = u(o + 4)
    p = o + 8
    for _ in range(count):
        size, typ, _unk, dsz = u(p), u(p + 4), u(p + 8), u(p + 12)
        data = b[p + 16:p + 16 + dsz]
        p += size
        w = lambda off: struct.unpack_from("<I", data, off)[0]
        if typ == VS and len(data) >= 0xDC:
            nregs, sz_off, ptr_off = 52, 0xD0, 0xD4
        elif typ == PS and len(data) >= 0xAC:
            nregs, sz_off, ptr_off = 41, 0xA4, 0xA8
        else:
            continue
        size_c, ptr = w(sz_off), w(ptr_off)
        if ptr + size_c > len(data):
            continue
        yield typ, [w(4 * i) for i in range(nregs)], data[ptr:ptr + size_c]


def archive_files(game):
    return [f for f in sorted(glob.glob(os.path.join(glob.escape(game), "**", "*"), recursive=True))
            if os.path.isfile(f) and f.endswith((".szs", ".pack", ".sarc", ".sharcfb"))]


def scan_archive(f, game):
    """distinct (type, microcode) -> (register words, container path) of one file, in file order"""
    found = {}
    with open(f, "rb") as stream:
        data = stream.read()
    for path, b in walk(data, os.path.relpath(f, game)):
        for typ, regs, code in sharcfb_shaders(b):
            found.setdefault((typ, code), (regs, path))
    return found


def default_jobs():
    """Archive scan workers: a few, leaving cores for the rest of the machine."""
    return max(1, min(4, (os.cpu_count() or 2) - 2))


def _scan_parallel(game, files, jobs, finished, threads=False):
    import concurrent.futures
    yaz0(b"")  # builds a source checkout's decoder once, before the workers load it
    results = [None] * len(files)
    # Threads overlap only the native Yaz0 decoding and file reads (ctypes releases the GIL);
    # used where processes cannot be started (Android's embedded Python).
    pool = (concurrent.futures.ThreadPoolExecutor if threads else concurrent.futures.ProcessPoolExecutor)(jobs)
    try:
        # Largest first: the long Yaz0 archives do not end up alone at the end.
        order = sorted(range(len(files)), key=lambda i: -os.path.getsize(files[i]))
        futures = {pool.submit(scan_archive, files[i], game): i for i in order}
        for future in concurrent.futures.as_completed(futures):
            i = futures[future]
            results[i] = future.result()
            finished(i)
    finally:
        try:
            pool.shutdown(wait=True, cancel_futures=True)
        except TypeError:  # Python < 3.9
            pool.shutdown(wait=True)
    return results


def game_shaders(game, progress=None, jobs=1, threads=False):
    """all distinct (type, microcode) -> (register words, container path)

    progress(done_bytes, total_bytes, done_files, total_files) follows the archive scan.
    jobs > 1 scans archives in worker processes (threads=True: threads); the result (and its
    order) is the same.
    """
    files = archive_files(game)
    sizes = [os.path.getsize(f) for f in files]
    total = sum(sizes)
    state = {"bytes": 0, "files": 0}
    def finished(i):
        state["bytes"] += sizes[i]
        state["files"] += 1
        if progress:
            progress(state["bytes"], total, state["files"], len(files))
    if progress:
        progress(0, total, 0, len(files))
    results = None
    if jobs > 1 and len(files) > 1:
        import concurrent.futures.process
        try:
            results = _scan_parallel(game, files, jobs, finished, threads)
        except (OSError, ImportError, NotImplementedError, concurrent.futures.process.BrokenProcessPool) as error:
            print("shaderprep: parallel scan unavailable (%s); scanning serially" % error, file=sys.stderr)
            state["bytes"] = state["files"] = 0
            results = None
    if results is None:
        results = []
        for i, f in enumerate(files):
            results.append(scan_archive(f, game))
            finished(i)
    out = {}
    for found in results:
        for key, value in found.items():
            out.setdefault(key, value)
    return out


# ---------------------------------------------------------------- recipe records
def decompress_bounded(data, limit, expected=None):
    decoder = zlib.decompressobj()
    raw = decoder.decompress(data, limit + 1)
    if len(raw) > limit or decoder.unconsumed_tail or not decoder.eof or decoder.unused_data:
        raise ValueError("invalid or oversized compressed recipe")
    if expected is not None and len(raw) != expected:
        raise ValueError("recipe decoded size mismatch")
    return raw


RECORD_TYPES = (1, 2, 3, 4, 5, 6)
# the runtime's Metal cache (shaders.bin) also holds a version header (7) and tombstones of stale pipeline
# recipes (8) (runtime/src/gfx/metal_cache_file.h); neither carries a state, so the tolerant reader skips them
CACHE_MARKER_TYPES = (7, 8)
RECORD_LIMIT = 16 * 1024 * 1024
# Player caches grow by appending (the Metal recorder never compacts shaders.bin); they are read one
# record at a time, so only the file itself is bounded.
CACHE_FILE_LIMIT = 4 * 1024 * 1024 * 1024
VULKAN_DATABASE_MAGIC = b"WWVKPR01"
VULKAN_LOCAL_DATABASE_MAGIC = b"WWVKPR02"  # the runtime's local database: shaders stored once (shaderprep_format)


# head-start bounds, as runtime/src/gfx/headstart_limits.h (the game's readers)
HEADSTART_MAX_FILE_BYTES = 2 * 1024 * 1024 * 1024
HEADSTART_MAX_RECORDS = 4000000
HEADSTART_MAX_RAW_BYTES = 8 * 1024 * 1024 * 1024


def read_records(path):
    """Strict reader for files this tool writes (head starts): any damage is an error."""
    if os.path.getsize(path) > HEADSTART_MAX_FILE_BYTES:
        raise ValueError("recipe file size limit")
    with open(path, "rb") as source:
        d = source.read()
    o = total = count = 0
    while o < len(d):
        if len(d) - o < 12:
            raise ValueError("truncated recipe header")
        t, raw, comp = struct.unpack_from("<III", d, o)
        o += 12
        count += 1
        total += raw
        if t not in RECORD_TYPES or not 0 < raw <= RECORD_LIMIT or \
                not 0 < comp <= RECORD_LIMIT or comp > len(d) - o or \
                count > HEADSTART_MAX_RECORDS or total > HEADSTART_MAX_RAW_BYTES:
            raise ValueError("invalid recipe bounds")
        yield t, decompress_bounded(d[o:o + comp], raw, raw)
        o += comp


class CacheReport:
    """What reading a player's caches found: records per type, and what was skipped and why."""
    def __init__(self):
        self.records = collections.Counter()
        self.skipped = collections.Counter()
        self.ignored_bytes = 0
        self.renderers = set()

    def skip(self, reason, count=1):
        self.skipped[reason] += count

    def warnings(self):
        lines = ["skipped %d %s" % (n, reason) for reason, n in sorted(self.skipped.items())]
        if self.ignored_bytes:
            lines.append("ignored %d damaged bytes" % self.ignored_bytes)
        return lines


def _record_at(d, o):
    """(type, raw size, compressed size, payload) of an intact record at offset o, else None."""
    if len(d) - o < 14:
        return None
    t, raw, comp = struct.unpack_from("<III", d, o)
    # every record is one zlib stream: CMF says deflate and the header checksum holds
    if t not in RECORD_TYPES + CACHE_MARKER_TYPES or not 0 < raw <= RECORD_LIMIT or not 2 <= comp <= RECORD_LIMIT or \
            comp > len(d) - o - 12 or d[o + 12] & 15 != 8 or int.from_bytes(d[o + 12:o + 14], "big") % 31:
        return None
    try:
        return t, raw, comp, decompress_bounded(d[o + 12:o + 12 + comp], raw, raw)
    except (ValueError, zlib.error):
        return None


def _next_record(d, o):
    """Offset of the next intact record after damage at o, or None (the rest is unusable)."""
    at = o + 1
    while True:
        # the zlib stream starts 12 bytes after a header; zlib's CMF byte is 0x?8 (0x78 in practice)
        at = d.find(b"\x78", at + 12) - 12
        if at < o + 1:
            return None
        if _record_at(d, at):
            return at
        at += 1


def read_cache_records(path, report=None):
    """Tolerant reader for a player's own caches, any version since shader preparation was added:
      - recipe streams (shaders.bin: Metal type 1 shader + type 2 pipeline records, and head starts
        with types 3 speculative, 4 Vulkan database, 5 code-free Vulkan state, 6 linked shader);
      - a Vulkan recipe database file (*.bin.recipes, "WWVKPR01" or the local "WWVKPR02"), yielded as one type 4
        record (read with vulkan_database_pipelines; never copied into a head start as it is).
    A damaged, partial or unknown record is skipped (counted in report) and reading resumes at the
    next intact record; nothing aborts the whole file."""
    report = report if report is not None else CacheReport()
    size = os.path.getsize(path)
    if size > CACHE_FILE_LIMIT:
        report.skip("cache file over %d GB (%s)" % (CACHE_FILE_LIMIT >> 30, os.path.basename(path)))
        return
    with open(path, "rb") as source:
        d = source.read()
    if d.startswith((VULKAN_DATABASE_MAGIC, VULKAN_LOCAL_DATABASE_MAGIC)):
        report.records[REC_VULKAN_PIPELINE] += 1
        yield REC_VULKAN_PIPELINE, d
        return
    o = 0
    while o < len(d):
        found = _record_at(d, o)
        if found:
            t, raw, comp, payload = found
            o += 12 + comp
            if t in CACHE_MARKER_TYPES:
                continue
            report.records[t] += 1
            yield t, payload
            continue
        resume = _next_record(d, o)
        if resume is None:
            report.skip("truncated or damaged record(s) at the end of %s" % os.path.basename(path))
            report.ignored_bytes += len(d) - o
            break
        report.skip("damaged record(s) in %s" % os.path.basename(path))
        report.ignored_bytes += resume - o
        o = resume


def parse_shader_record(r):
    if len(r)<16: raise ValueError('truncated shader record')
    vertex, size, fs_size, n = struct.unpack_from('<IIII',r)
    if vertex not in (0,1) or not 0<size<=16*1024*1024 or fs_size>4096 or n>16384 or 16+size+fs_size+n*8!=len(r):
        raise ValueError('invalid shader record bounds')
    o=16
    code=bytes(r[o:o+size]);o+=size
    fs=bytes(r[o:o+fs_size]);o+=fs_size
    regs=[struct.unpack_from('<II',r,o+8*i) for i in range(n)]
    return vertex,code,fs,regs


def parse_linked_shader_record(raw):
    if len(raw)<20: raise ValueError('truncated linked shader record')
    vertex,ps,fs,n,vs=struct.unpack_from('<5I',raw)
    if vertex!=0 or not 0<ps<=16*1024*1024 or not 0<vs<=16*1024*1024 or not 0<fs<=4096 or n>16384 or 20+ps+fs+vs+n*8!=len(raw):
        raise ValueError('invalid linked shader bounds')
    o=20
    pixel=raw[o:o+ps];o+=ps
    fetch=raw[o:o+fs];o+=fs
    companion=raw[o:o+vs];o+=vs
    regs=[struct.unpack_from('<II',raw,o+i*8) for i in range(n)]
    return pixel,fetch,companion,regs


def linked_shader_record(pixel,fetch,companion,regs):
    return struct.pack('<5I',0,len(pixel),len(fetch),len(regs),len(companion))+pixel+fetch+companion+b''.join(struct.pack('<II',*r) for r in regs)


def write_record(f, t, raw):
    write_compressed(f, t, raw, zlib.compress(raw, 6))


def write_compressed(f, t, raw, comp):
    f.write(struct.pack("<III", t, len(raw), len(comp)))
    f.write(comp)


def shader_record(vertex, code, fs, regs):
    raw = struct.pack("<IIII", vertex, len(code), len(fs), len(regs)) + code + fs
    raw += b"".join(struct.pack("<II", r, v) for r, v in regs)
    return raw


def prog_hash(code):
    """FNV-1a style over little-endian 64-bit words (the last one zero-padded), xor length"""
    n = len(code)
    whole = n // 8
    h = 0xCBF29CE484222325
    for word in struct.unpack_from("<%dQ" % whole, code):
        h = ((h ^ word) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    if n % 8:
        h = ((h ^ int.from_bytes(code[whole * 8:], "little")) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h ^ n


# ---------------------------------------------------------------- shader register blocks
# registers GX2SetVertexShader / GX2SetPixelShader write from the shader structure
# (same sequence as runtime/src/gx2/gx2_shader_regs.cpp)
mmSQ_PGM_START_PS, mmSQ_PGM_RESOURCES_PS = 0xA210, 0xA214
mmSQ_PGM_START_VS, mmSQ_PGM_RESOURCES_VS, mmSQ_PGM_START_FS = 0xA216, 0xA21A, 0xA225
mmVGT_PRIMITIVEID_EN, mmSPI_VS_OUT_CONFIG, mmPA_CL_VS_OUT_CNTL = 0xA2A1, 0xA1B1, 0xA207
mmSPI_VS_OUT_ID_0, mmSQ_VTX_SEMANTIC_0, mmSQ_VTX_SEMANTIC_CLEAR = 0xA185, 0xA0E0, 0xA238
mmSPI_PS_IN_CONTROL_0, mmSPI_PS_INPUT_CNTL_0 = 0xA1B3, 0xA191
mmCB_SHADER_MASK, mmCB_SHADER_CONTROL, mmDB_SHADER_CONTROL, mmSPI_INPUT_Z = 0xA08F, 0xA1E8, 0xA203, 0xA1B6


def own_block(vertex, w):
    """register -> value pairs a shader structure sets (w = structure register words)"""
    r = {}
    if vertex:
        r[mmSQ_PGM_RESOURCES_VS] = w[0x00 // 4]
        r[mmVGT_PRIMITIVEID_EN] = w[0x04 // 4]
        r[mmSPI_VS_OUT_CONFIG] = w[0x08 // 4]
        r[mmPA_CL_VS_OUT_CNTL] = w[0x38 // 4]
        for i in range(min(w[0x0C // 4], 10)):
            r[mmSPI_VS_OUT_ID_0 + i] = w[0x10 // 4 + i]
        nsem = min(w[0x40 // 4], 32)
        if nsem:
            r[mmSQ_VTX_SEMANTIC_CLEAR] = 0xFFFFFFFF
            for i in range(nsem):
                r[mmSQ_VTX_SEMANTIC_0 + i] = w[0x44 // 4 + i]
    else:
        r[mmSQ_PGM_RESOURCES_PS] = w[0]
        r[mmSPI_PS_IN_CONTROL_0] = w[2]
        r[mmSPI_PS_IN_CONTROL_0 + 1] = w[3]
        for i in range(min(w[4], 0x20)):
            r[mmSPI_PS_INPUT_CNTL_0 + i] = w[5 + i]
        r[mmCB_SHADER_MASK] = w[37]
        r[mmCB_SHADER_CONTROL] = w[38]
        r[mmDB_SHADER_CONTROL] = w[39]
        r[mmSPI_INPUT_Z] = w[40]
    return r


# ---------------------------------------------------------------- template
def _codefree_state(h, vertex, fs, regs, linked=None):
    # ALU constant banks contain per-draw uniform values, not translation state.
    # Vulkan records whole context banks; never publish these values. Pixel
    # translation does not use fetch identity (translate seed is vertex-only).
    fetch = (fs if isinstance(fs, dict) else template_format.parse_fetch(fs, regs, prog_hash)) if vertex else None
    drop = (mmSQ_PGM_START_VS, mmSQ_PGM_START_PS, mmSQ_PGM_START_FS)
    state = {'hash': h, 'vertex': vertex, 'fetch': fetch,
             'registers': [[a, v] for a, v in regs if a not in drop and not 0xc000 <= a < 0xe000]}
    if linked is not None: state['linked_vertex']=linked
    return state


def _codefree_pipeline(kind, payload):
    if isinstance(payload, dict): return payload
    if kind == REC_PIPELINE: return template_format.metal_pipeline(payload)
    if kind == 5 and payload.startswith(b'WWVKST01'):
        return template_format.vulkan_recipe(payload[8:], codefree=True)
    raise ValueError('unsupported code-free pipeline type')


def vulkan_database_pipelines(data, report):
    """Code-free pipelines of a local Vulkan recipe database; a damaged recipe is skipped, not fatal."""
    if len(data) < 64 or not data.startswith((VULKAN_DATABASE_MAGIC, VULKAN_LOCAL_DATABASE_MAGIC)):
        report.skip("unreadable Vulkan recipe database")
        return []
    count, crc = struct.unpack_from("<II", data, 56)
    if zlib.crc32(data[64:]) != crc:
        report.skip("Vulkan recipe database with a bad checksum (recipes checked one by one)")
    try:
        shaders, at = template_format.vulkan_database_shaders(data)
    except ValueError:
        report.skip("Vulkan recipes missing from a truncated database", count)
        return []
    out = []
    for i in range(count):
        if at + 4 > len(data):
            report.skip("Vulkan recipes missing from a truncated database", count - i)
            break
        size = struct.unpack_from("<I", data, at)[0]
        at += 4
        if size > 4 * 1024 * 1024 or size > len(data) - at:
            report.skip("Vulkan recipes missing from a truncated database", count - i)
            break
        try:
            out.append(template_format.vulkan_recipe(data[at:at + size], shaders=shaders))
        except ValueError as error:
            report.skip("Vulkan recipe(s) of an unsupported layout (%s)" % error)
        at += size
    return out


def _canonical(value):
    # a digest, not the JSON text: a full-game template has ~90,000 states of ~3 KB JSON each
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).digest()


class TemplateBuilder:
    """Collects de-duplicated code-free states and pipelines from caches and templates."""
    def __init__(self, report=None):
        self.report = report if report is not None else CacheReport()
        self.states, self.pipelines = {}, {}

    def add_state(self, state):
        self.states.setdefault(_canonical(state), state)

    def add_pipeline(self, pipeline):
        self.pipelines.setdefault(_canonical(pipeline), pipeline)
        self.report.renderers.add(pipeline["backend"])

    def add_template(self, path, allow_legacy=True):
        old_states, old_pipelines = read_template(path, allow_legacy=allow_legacy)
        for s in old_states:
            self.add_state(_codefree_state(*s))
        for kind, p in old_pipelines:
            if kind == REC_VULKAN_PIPELINE and not isinstance(p, dict):
                for q in template_format.vulkan_database(p):
                    self.add_pipeline(q)
            else:
                self.add_pipeline(_codefree_pipeline(kind, p))

    def add_record(self, kind, raw):
        if kind == REC_VULKAN_PIPELINE:
            for p in vulkan_database_pipelines(raw, self.report):
                self.add_pipeline(p)
        elif kind in (REC_PIPELINE, 5):
            self.add_pipeline(_codefree_pipeline(kind, raw))
        elif kind == 6:
            code, fs, vs, regs = parse_linked_shader_record(raw)
            self.add_state(_codefree_state(prog_hash(code), 0, b"", regs,
                                           {"hash": prog_hash(vs), "fetch": template_format.parse_fetch(fs, regs, prog_hash)}))
        elif kind == REC_SHADER:
            vertex, code, fs, regs = parse_shader_record(raw)
            self.add_state(_codefree_state(prog_hash(code), vertex, fs, regs))
            self.report.renderers.add("metal")
        else:  # REC_SPECULATIVE: guessed by a head-start build, never recorded by the game
            self.report.skip("speculative head-start record(s) (not recorded states)")

    def add_cache(self, path):
        for kind, raw in read_cache_records(path, self.report):
            try:
                self.add_record(kind, raw)
            except (ValueError, struct.error) as error:
                self.report.skip("record(s) of type %d the template cannot hold (%s)" % (kind, error))

    def document(self):
        states, pipelines = list(self.states.values()), list(self.pipelines.values())
        # the schema (and the release guard) hold at most TEMPLATE_MAX entries of each kind
        if len(states) > TEMPLATE_MAX:
            self.report.skip("states over the template limit of %d (the first ones are kept)" % TEMPLATE_MAX,
                             len(states) - TEMPLATE_MAX)
            states = states[:TEMPLATE_MAX]
        if len(pipelines) > TEMPLATE_MAX:
            self.report.skip("pipelines over the template limit of %d (the first ones are kept)" % TEMPLATE_MAX,
                             len(pipelines) - TEMPLATE_MAX)
            pipelines = pipelines[:TEMPLATE_MAX]
        return {"version": 3, "states": states, "pipelines": pipelines}  # encode() validates it


TEMPLATE_MAX = template_format.MAX_ENTRIES


def write_template(doc, out, guarded=False):
    """Encode and write a code-free template. guarded: also pass the release guard's independent check
    first (export/merge: files meant to leave this computer); nothing is written if it fails."""
    out = os.fspath(out)
    data = template_format.encode(doc)
    if guarded:
        guard_template(data)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    temporary = out + ".tmp"
    with open(temporary, "wb") as output:
        output.write(data)
    os.replace(temporary, out)
    return data


def guard_template(data):
    """The release guard's code-free template check (tools/release/guard.py, its own implementation of
    the schema). Raises ValueError unless data is a code-free WWHT0003 template."""
    sys.path.insert(0, os.path.join(HERE, "release"))
    try:
        import guard
    except ImportError:
        raise ValueError("the release guard (tools/release/guard.py) is missing: refusing to write a template "
                         "that was not checked")
    finally:
        sys.path.pop(0)
    # check_entry: the guard's whole release check of the one approved template path, including
    # validate_code_free_template (the guard's own schema implementation)
    problems = []
    guard.check_entry("tools/shadercache/template.bin", data, problems)
    if problems:
        raise ValueError("the release guard rejected the template: " + "; ".join(problems))


def cmd_template(cache, out, merge=None):
    builder = TemplateBuilder()
    if merge:
        builder.add_template(merge)
    builder.add_cache(cache)
    for line in builder.report.warnings():
        print("warning: " + line)
    doc = builder.document()
    data = write_template(doc, out)
    print('code-free template: %d states, %d pipelines, %d bytes -> %s' %
          (len(doc['states']), len(doc['pipelines']), len(data), out))


# ---------------------------------------------------------------- export (issue #177)
ISSUE_URL = "https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues/177"
EXPORT_NAME = "my-template.bin"


def user_config_dirs(environ=None, platform=None, root=ROOT):
    """The game's settings folders (runtime: host::config_dir), most specific first: WWHD_USER_DIR, a
    portable release's data/user, then the per-user folder (macOS Application Support, Windows
    %APPDATA%, Linux and the AppImage $XDG_CONFIG_HOME or ~/.config)."""
    environ = os.environ if environ is None else environ
    platform = sys.platform if platform is None else platform
    home = environ.get("HOME") or os.path.expanduser("~")
    out = []
    if environ.get("WWHD_USER_DIR"):
        out.append(environ["WWHD_USER_DIR"])
    out += [os.path.join(root, "data", "user"), os.path.join(root, "user")]
    if platform == "darwin":
        out.append(os.path.join(home, "Library", "Application Support", "WWHD"))
    elif platform.startswith("win"):
        if environ.get("APPDATA"):
            out.append(os.path.join(environ["APPDATA"], "WWHD"))
    else:
        out.append(os.path.join(environ["XDG_CONFIG_HOME"], "wwhd") if environ.get("XDG_CONFIG_HOME")
                   else os.path.join(home, ".config", "wwhd"))
    seen, unique = set(), []
    for d in out:
        key = os.path.normcase(os.path.abspath(d))
        if key not in seen:
            seen.add(key)
            unique.append(d)
    return unique


def cache_files_in(directory):
    """The recipe caches in one folder: shaders.bin and the Vulkan *.recipes databases. Never the
    driver pipeline caches (*.bin) or the SPIR-V folder (vulkan-shaders/)."""
    files = []
    metal = os.path.join(directory, "shaders.bin")
    if os.path.isfile(metal):
        files.append(metal)
    files += sorted(f for f in glob.glob(os.path.join(glob.escape(directory), "*.recipes")) if os.path.isfile(f))
    return files


def find_player_caches(source=None, environ=None, platform=None, root=ROOT):
    """(folder, cache files): --from DIR (or one cache file), else the first live cache folder
    (<settings>/shader-prebuild/live) that has caches, else the legacy locations of earlier releases."""
    if source:
        if os.path.isfile(source):
            return os.path.dirname(source) or ".", [source]
        live = os.path.join(source, "shader-prebuild", "live")
        for d in (source, live, os.path.join(source, "user", "shader-prebuild", "live"),
                  os.path.join(source, "data", "user", "shader-prebuild", "live")):
            if os.path.isdir(d) and cache_files_in(d):
                return d, cache_files_in(d)
        return source, []
    environ = os.environ if environ is None else environ
    platform = sys.platform if platform is None else platform
    configs = user_config_dirs(environ, platform, root)
    for config in configs:
        d = os.path.join(config, "shader-prebuild", "live")
        if cache_files_in(d):
            return d, cache_files_in(d)
    # before the live folder (releases up to v0.2.10): shaders.bin and Vulkan recipes in the settings
    # folder, its shadercache/ subfolder, and on macOS ~/Library/Caches/wwhd/shaders.bin
    home = environ.get("HOME") or os.path.expanduser("~")
    legacy = []
    if platform == "darwin":
        legacy.append(os.path.join(home, "Library", "Caches", "wwhd"))
    for config in configs:
        legacy += [config, os.path.join(config, "shadercache")]
    for d in legacy:
        if cache_files_in(d):
            return d, cache_files_in(d)
    return None, []


RENDERER_NAMES = {"metal": "Metal", "vulkan": "Vulkan"}


def cmd_export(out=None, source=None):
    """One step for players: their own caches -> a code-free template they can attach to issue #177."""
    out = out or os.path.join(ROOT, EXPORT_NAME)
    if os.path.isdir(out):
        out = os.path.join(out, EXPORT_NAME)
    folder, files = find_player_caches(source)
    if not files:
        print("No shader cache found%s. Play a while first (any renderer), or give the folder with "
              "--from DIR (it holds shaders.bin or *.recipes)." % (" in " + folder if folder else ""))
        return 1
    print("Reading the shader caches in %s" % folder)
    builder = TemplateBuilder()
    for path in files:
        before_states, before_pipelines = len(builder.states), len(builder.pipelines)
        builder.add_cache(path)
        print("  %s: %d new states, %d new pipelines" % (os.path.basename(path), len(builder.states) - before_states,
                                                         len(builder.pipelines) - before_pipelines))
    for line in builder.report.warnings():
        print("warning: " + line)
    doc = builder.document()
    if not doc["states"] and not doc["pipelines"]:
        print("The caches hold nothing a template can use; nothing written.")
        return 1
    try:
        data = write_template(doc, out, guarded=True)
    except ValueError as error:
        print("Not written: %s" % error)
        return 1
    renderers = ", ".join(RENDERER_NAMES.get(r, r) for r in sorted(builder.report.renderers)) or "none"
    print("Wrote %s: %d shader states, %d pipelines (renderer: %s), %d bytes." %
          (out, len(doc["states"]), len(doc["pipelines"]), renderers, len(data)))
    print("It holds only hashes, register values and pipeline settings: no shader code, no game data.")
    print("Attach it to issue #177: " + ISSUE_URL)
    print("WWHD-EXPORT %d %d %s %s" % (len(doc["states"]), len(doc["pipelines"]),
                                       "+".join(sorted(builder.report.renderers)) or "none", out), flush=True)
    return 0


def cmd_stage_template(out, cache, base, source=None):
    """Mod makers: what the caches recorded while playing a mod's stages have beyond the base template (the
    release's), as a code-free template the package ships; setup prepares it with the game's own."""
    if not os.path.isfile(base):
        print("No base template at %s (give it with --base)" % base)
        return 1
    if cache:
        files = [cache]
    else:
        folder, files = find_player_caches(source)
        if not files:
            print("No shader cache found%s. Play the stage first, or give the folder with --from DIR." % (" in " + folder if folder else ""))
            return 1
        print("Reading the shader caches in %s" % folder)
    known = TemplateBuilder()
    known.add_template(base)
    builder = TemplateBuilder()
    for path in files:
        builder.add_cache(path)
    for key in list(builder.states):
        if key in known.states:
            del builder.states[key]
    for key in list(builder.pipelines):
        if key in known.pipelines:
            del builder.pipelines[key]
    for line in builder.report.warnings():
        print("warning: " + line)
    doc = builder.document()
    try:
        data = write_template(doc, out, guarded=True)
    except ValueError as error:
        print("Not written: %s" % error)
        return 1
    print("stage template: %d states, %d pipelines beyond %s, %d bytes -> %s" %
          (len(doc["states"]), len(doc["pipelines"]), base, len(data), out))
    return 0


def cmd_merge(out, inputs):
    """Maintainers: merge players' code-free templates (de-duplicated); inputs and output pass the guard."""
    builder = TemplateBuilder()
    for path in inputs:
        with open(path, "rb") as source:
            data = source.read()
        try:
            guard_template(data)
        except ValueError as error:
            print("Not merged: %s: %s" % (path, error))
            return 1
        before_states, before_pipelines = len(builder.states), len(builder.pipelines)
        builder.add_template(path, allow_legacy=False)
        print("  %s: %d new states, %d new pipelines" % (path, len(builder.states) - before_states,
                                                         len(builder.pipelines) - before_pipelines))
    for line in builder.report.warnings():
        print("warning: " + line)
    doc = builder.document()
    try:
        data = write_template(doc, out, guarded=True)
    except ValueError as error:
        print("Not written: %s" % error)
        return 1
    print("merged template: %d states, %d pipelines, %d bytes -> %s" %
          (len(doc["states"]), len(doc["pipelines"]), len(data), out))
    return 0


def read_template(path, allow_legacy=False):
    with open(path,'rb') as source: data=source.read()
    if data.startswith(template_format.MAGIC):
        # compact: states arrive as validated (hash, vertex, fetch, registers, linked) tuples with packed
        # registers; a full-game template then needs about 1 GB less (phones prepare shaders too)
        doc=template_format.decode(data, compact=True)
        states=doc['states']
        return states, [(5 if p['backend']=='vulkan' else REC_PIPELINE,p) for p in doc['pipelines']]
    if not allow_legacy:
        raise ValueError('legacy template carries fetch bytes; convert locally with template --merge')
    return read_legacy_template(path)


def read_legacy_template(path):
    if not os.path.isfile(path):
        sys.exit("no state template at %s\n"
                 "make one from your own runtime cache after playing for a while:\n"
                 "  tools/shaderprep.py template ~/Library/Caches/wwhd/shaders.bin" % path)
    with open(path, "rb") as source:
        d = source.read()
    if d[:4] != b"WWHT":
        sys.exit("not a template: " + path)
    typed = d.startswith(b"WWHT0002")
    raw = decompress_bounded(d[8:] if typed else d[4:], 256 * 1024 * 1024)
    o = 4
    fetch = []
    for _ in range(struct.unpack_from("<I", raw, 0)[0]):
        ln = struct.unpack_from("<I", raw, o)[0]
        fetch.append(raw[o + 4:o + 4 + ln])
        o += 4 + ln
    n = struct.unpack_from("<I", raw, o)[0]
    o += 4
    states = []
    for _ in range(n):
        h, vertex, fi, nr = struct.unpack_from("<QIiI", raw, o)
        o += 20
        regs = [struct.unpack_from("<II", raw, o + 8 * i) for i in range(nr)]
        o += 8 * nr
        states.append((h, vertex, fetch[fi] if fi >= 0 else b"", regs, None))
    pipelines = []
    if o + 4 <= len(raw):  # older templates end here
        n = struct.unpack_from("<I", raw, o)[0]
        o += 4
        for _ in range(n):
            kind = REC_PIPELINE
            if typed:
                kind = struct.unpack_from("<I", raw, o)[0]
                o += 4
                if kind not in (REC_PIPELINE, REC_VULKAN_PIPELINE):
                    raise ValueError("unknown template pipeline type")
            ln = struct.unpack_from("<I", raw, o)[0]
            if ln > len(raw) - o - 4:
                raise ValueError("truncated template pipeline")
            pipelines.append((kind, raw[o + 4:o + 4 + ln]))
            o += 4 + ln
    if o != len(raw):
        raise ValueError("trailing template bytes")
    return states, pipelines


# ---------------------------------------------------------------- commands
def cmd_scan(game):
    shaders = game_shaders(game)
    per = collections.Counter(p.split("/")[0] + "/" + os.path.basename(p) for (_, (_, p)) in shaders.items())
    kinds = collections.Counter(t for t, _ in shaders)
    print("distinct shaders: %d (VS %d, PS %d)" % (len(shaders), kinds[VS], kinds[PS]))
    for k, v in per.most_common(20):
        print("%6d  %s" % (v, k))


def cmd_coverage(game, cache):
    shaders = game_shaders(game)
    progs = set()
    report = CacheReport()
    for t, r in read_cache_records(cache, report):
        try:
            if t == 6:
                code,fs,vs,regs=parse_linked_shader_record(r)
                progs.add((PS,code));progs.add((VS,vs))
            elif t in (REC_SHADER, REC_SPECULATIVE):
                vertex, code, fs, regs = parse_shader_record(r)
                progs.add((VS if vertex else PS, code))
        except (ValueError, struct.error) as error:
            report.skip("unreadable shader record(s) (%s)" % error)
    for line in report.warnings():
        print("warning: " + line)
    have = sum(1 for k in progs if k in shaders)
    print("programs: %d, found in archives: %d (%.1f%%)" % (len(progs), have, 100.0 * have / max(len(progs), 1)))
    missing = [k for k in progs if k not in shaders]
    print("missing by type:", dict(collections.Counter("VS" if t == VS else "PS" for t, _ in missing)))


# Share of the build progress the archive scan takes; writing the records takes the rest.
SCAN_SHARE = 0.85


def cmd_build(game, out, template, limit, with_pipelines=True, progress=None, jobs=1, threads=False):
    """progress(done, total, detail): done/total in thousandths, monotonic over the whole build.
    jobs > 1 scans archives and compresses records in parallel; the output is byte-identical.
    threads=True scans on threads instead of processes (embedded Python, e.g. Android)."""
    import threading
    def report(fraction, detail):
        if progress:
            progress(int(1000 * min(1.0, fraction)), 1000, detail)
    loaded = {}
    def load():
        try:
            loaded["value"] = read_template(template)
        except BaseException as error:
            loaded["error"] = error
    # The template is validated while the archives are scanned.
    reader = threading.Thread(target=load, name="template", daemon=True)
    reader.start()
    def scanned(done_bytes, total_bytes, done_files, total_files):
        report(SCAN_SHARE * done_bytes / max(total_bytes, 1),
               "%s of %s game archives" % (format(done_files, ","), format(total_files, ",")))
    try:
        shaders = (game_shaders(game, scanned, jobs, threads) if progress else
                   game_shaders(game, jobs=jobs, threads=threads))
    finally:
        reader.join()
    if "error" in loaded:
        raise loaded["error"]
    states, pipelines = loaded["value"]
    if not with_pipelines:
        pipelines = []
    hashes = {}
    def program_hash(code):
        h = hashes.get(code)
        if h is None:
            h = hashes[code] = prog_hash(code)
        return h
    by_hash = collections.defaultdict(list)
    for h, vertex, fs, regs, linked in states:
        by_hash[(h, bool(vertex))].append((fs, regs, linked))
    # speculative states: those recorded for programs with an identical shader register block
    # (none are written with --max 0, which setup uses: skip the states x blocks pass then)
    by_family = collections.defaultdict(list)
    if limit > 0:
        blocks = {}
        for (typ, code), (w, path) in shaders.items():
            blocks.setdefault((typ == VS, tuple(sorted(own_block(typ == VS, w)))), None)
        for h, vertex, fs, regs, linked in states:
            regmap = dict(regs)
            for (tv, regset) in blocks:
                if tv == bool(vertex):
                    by_family[(tv, tuple((a, regmap.get(a, 0)) for a in regset))].append((fs, regs, linked))
    pgm = {True: mmSQ_PGM_START_VS, False: mmSQ_PGM_START_PS}
    companion_programs=collections.defaultdict(set)
    for (typ,code) in shaders:
        if typ==VS: companion_programs[program_hash(code)].add(code)
    counts = {"unresolved_linked": 0, "known": 0, "spec": 0}
    def records():
        for (typ, code), (w, path) in shaders.items():
            vertex = typ == VS
            for fs, regs, linked in by_hash.get((program_hash(code), vertex), []):
                r = dict(regs)
                r[pgm[vertex] + 1] = len(code) >> 3  # the loader supplies the program address
                if linked is not None:
                    matches=companion_programs.get(linked['hash'],set())
                    if len(matches)!=1:
                        counts["unresolved_linked"]+=1
                        continue
                    companion=next(iter(matches))
                    r[mmSQ_PGM_START_VS+1]=len(companion)>>3
                    yield 6, linked_shader_record(code,template_format.fetch_payload(linked['fetch']),companion,sorted(r.items()))
                else:
                    yield REC_SHADER, shader_record(1 if vertex else 0, code, template_format.fetch_payload(fs) if fs else b"", sorted(r.items()))
                counts["known"] += 1
        for kind, p in pipelines:
            yield kind, template_format.vulkan_payload(p) if kind==5 else template_format.metal_payload(p)
        for (typ, code), (w, path) in shaders.items():
            vertex = typ == VS
            if (program_hash(code), vertex) in by_hash:
                continue
            blk = own_block(vertex, w)
            for fs, regs, linked in by_family.get((vertex, tuple(sorted(blk.items()))), [])[:limit]:
                if linked is not None: continue # Never invent a linked VS for a speculative PS.
                r = dict(regs)
                r.update(blk)
                r[pgm[vertex] + 1] = len(code) >> 3
                yield REC_SPECULATIVE, shader_record(1 if vertex else 0, code, template_format.fetch_payload(fs) if fs else b"", sorted(r.items()))
                counts["spec"] += 1
    # Progress while writing: the known records are bounded by the matching template states.
    expected = max(1, sum(len(by_hash.get((program_hash(code), typ == VS), ())) for (typ, code) in shaders) + len(pipelines))
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    written = 0
    def wrote(n):
        report(SCAN_SHARE + (1 - SCAN_SHARE) * min(1.0, n / expected),
               "%s of %s shader records" % (format(min(n, expected), ","), format(expected, ",")))
    with open(out, "wb") as f:
        if jobs > 1:
            # zlib releases the GIL: compress batches on threads, write in the original order
            import concurrent.futures
            import itertools
            with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
                stream = records()
                while True:
                    batch = list(itertools.islice(stream, 256))
                    if not batch:
                        break
                    for (t, raw), comp in zip(batch, pool.map(lambda item: zlib.compress(item[1], 6), batch)):
                        write_compressed(f, t, raw, comp)
                    written += len(batch)
                    wrote(written)
        else:
            for t, raw in records():
                write_record(f, t, raw)
                written += 1
                if written % 256 == 0:
                    wrote(written)
    report(1.0, "%s shader records" % format(written, ","))
    print("unresolved linked shader states: %d" % counts["unresolved_linked"])
    print("archive shaders %d; known records %d; pipeline records %d; speculative records %d -> %s (%d bytes)" %
          (len(shaders), counts["known"], len(pipelines), counts["spec"], out, os.path.getsize(out)))


def main():
    args = sys.argv[1:]
    opts = {"--game": os.path.join(ROOT, "game", "content"), "--max": "2", "--template": TEMPLATE, "--merge": None, "--from": None, "--base": None,
            "--jobs": str(default_jobs())}
    no_pipelines = "--no-pipelines" in args
    if no_pipelines:
        args.remove("--no-pipelines")
    machine_progress = "--progress" in args
    if machine_progress:
        args.remove("--progress")
    for k in list(opts):
        if k in args:
            i = args.index(k)
            opts[k] = args[i + 1]
            del args[i:i + 2]
    if not args:
        print(__doc__)
        return
    cmd, game = args[0], opts["--game"]
    if cmd == "scan":
        cmd_scan(game)
    elif cmd == "coverage":
        cmd_coverage(game, args[1])
    elif cmd == "template":
        cmd_template(args[1], args[2] if len(args) > 2 else TEMPLATE, opts["--merge"])
    elif cmd == "export":
        sys.exit(cmd_export(args[1] if len(args) > 1 else None, opts["--from"]))
    elif cmd == "stage-template":
        if len(args) < 2:
            sys.exit("usage: shaderprep.py stage-template OUT [CACHE] [--from DIR] [--base TEMPLATE]")
        sys.exit(cmd_stage_template(args[1], args[2] if len(args) > 2 else None, opts["--base"] or opts["--template"], opts["--from"]))
    elif cmd == "merge":
        if len(args) < 3:
            sys.exit("usage: shaderprep.py merge OUT TEMPLATE1 [TEMPLATE2 ...]")
        sys.exit(cmd_merge(args[1], args[2:]))
    elif cmd == "build":
        out = args[1] if len(args) > 1 else os.path.join(ROOT, "game", "shadercache", "headstart.bin")
        def progress(done, total, detail):
            # setup (tools/installer/shader_prebuild.py) parses these lines; at most ~10 per second
            now = time.monotonic()
            if done < total and now - progress.last < 0.1:
                return
            progress.last = now
            print("WWHD-PROGRESS %d %d %s" % (done, total, detail), flush=True)
        progress.last = 0.0
        cmd_build(game, out, opts["--template"], int(opts["--max"]), not no_pipelines,
                  progress if machine_progress else None, max(1, int(opts["--jobs"])))
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
