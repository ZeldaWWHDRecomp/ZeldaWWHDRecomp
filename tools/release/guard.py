#!/usr/bin/env python3
"""Release guard: rejects game code, caches and keys; permits one strictly code-free state template.

usage: guard.py [--allow-android-test-fixtures] ARTIFACT.zip|DIR [...]

Release packages may contain only this project's runtime, tools and installer (plus third-party
licenses). This check rejects, by name and by content:
  - generated/recompiled game code (code_NNN.c, funcs.h with guest functions, table.c, imports.c,
    imports.json, report.txt from tools/recomp), and real-looking generated code inside any file;
  - game files: *.rpx, *.rpl, *.wux, *.wud, disc keys (*.key), tickets/TMDs (title.tik, title.tmd),
    saves (cking*.sav, *.sav), anything under a game/ code/ content/ meta/ tree;
  - shader caches and head starts; only independently validated WWHT0003 at
    tools/shadercache/template.bin is allowed (hashes, registers and typed state only);
  - in every text file: a 32-hex-digit string (the shape of a Wii U key; SHA-256 sums are 64 digits
    and git hashes 40, which do not match).
The Windows release's tools/python/ (the official embeddable Python) must hold exactly the files listed
in tools/release/python-windows-files.json, each with its listed SHA-256.
An AppImage tree (a folder with AppRun at its root, e.g. the extracted squashfs-root) must carry the
AppImage runtime's license texts (tools/release/appimage-runtime-licenses, copied by appimage.py)
unchanged in third-party-licenses/.
Android Python source archives must not contain debug fixture registration or smoke
entry points. Only debug CI artifacts may opt in with --allow-android-test-fixtures.
Exit status 1 lists every problem.
"""
import io
import hashlib
import json
import os
import re
import struct
import sys
import zipfile
import zlib
import xml.etree.ElementTree as ET

BAD_NAME = [
    (re.compile(r"(^|/)code_\d+\.c$"), "generated game code"),
    (re.compile(r"(^|/)(table|imports)\.c$"), "generated game code tables"),
    (re.compile(r"(^|/)imports\.json$"), "generated import table"),
    (re.compile(r"(^|/)funcs\.h$"), "generated function list"),
    (re.compile(r"\.(rpx|rpl|wux|wud|iso|wua|rvz|gci)$", re.I), "game executable or disc image"),
    (re.compile(r"\.key$", re.I), "key file"),
    (re.compile(r"(^|/)title\.(tik|tmd|cert)$", re.I), "disc ticket/metadata"),
    (re.compile(r"\.sav$", re.I), "save file"),
    (re.compile(r"(^|/)(shaders|headstart|template|shaderprep_template|spirv|vkpipelines|testcache|mycache)\.bin$", re.I), "shader cache"),
    (re.compile(r"\.(metallib|spv)$", re.I), "compiled shader cache"),
    (re.compile(r"(^|/)(shadercache|vulkan-shaders)/", re.I), "local shader cache tree"),
    (re.compile(r"(^|/)(game|content|meta)/"), "game file tree"),
    (re.compile(r"(^|/)build/gen"), "recompiler output"),
    (re.compile(r"(^|/)(lib)?gamecode\.(a|lib)$"), "compiled game code"),
    (re.compile(r"(^|/)common\.key$", re.I), "Wii U common key"),
]
TEXT_EXT = {".py", ".txt", ".md", ".json", ".sh", ".command", ".bat", ".ps1", ".h", ".hpp", ".c", ".cpp", ".inl",
            ".cfg", ".ini", ".xml", ".plist", ".toml", ".yml", ".yaml", ".rsp", ""}
KEYLIKE = re.compile(rb"(?<![0-9A-Fa-f])[0-9A-Fa-f]{32}(?![0-9A-Fa-f])")
# a function body as tools/recomp/recomp.py emits it (stubgen placeholders call ppc_unimplemented)
GEN_CODE = re.compile(rb"void f_[0-9A-F]{8}\(Cpu\* __restrict c\) \{\n")


PYTHON_FILES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-windows-files.json")
PYTHON_FILES_ARM64 = os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-windows-arm64-files.json")
PYTHON_DIR = re.compile(r"(?:^|/)tools/python/(.+)$")


def python_files(path):
    """The file list of the embeddable Python this release must ship: the ARM64 one in a windows-arm64
    release (its name: WindWakerHD-<version>-windows-arm64[.zip])."""
    return PYTHON_FILES_ARM64 if "windows-arm64" in os.path.basename(os.path.normpath(path)) else PYTHON_FILES


def check_python(name, data, seen, problems, files=PYTHON_FILES):
    """tools/python/...: only the pinned embeddable Python's own files, unmodified. Returns True when it was one."""
    m = PYTHON_DIR.search(name.replace("\\", "/"))
    if not m:
        return False
    with open(files) as f:
        expected = json.load(f)["files"]
    rel = m.group(1)
    if rel not in expected:
        problems.append("%s: not a file of the pinned embeddable Python" % name)
    elif hashlib.sha256(data).hexdigest() != expected[rel]:
        problems.append("%s: differs from the pinned embeddable Python" % name)
    seen.add(rel)
    return True



# Fetch provenance stores two uint64 identities (Vulkan and Metal) plus a
# compact-layout flag; these are metadata, never raw fetch instruction bytes.
# Independent implementation of the release exception; deliberately never import
# shaderprep or its format reader. Any schema evolution requires guard review.
SAFE_TEMPLATE_MAGIC = b"WWHT0003"
# Same limits as tools/shaderprep_format.py (MAX_BYTES, MAX_ENTRIES): a full-game template decodes to
# about 300 MiB of JSON with about 90,000 states.
TEMPLATE_MAX_BYTES = 1024 * 1024 * 1024
TEMPLATE_MAX_ENTRIES = 1000000
SAFE_TEMPLATE_PATH = re.compile(r"^(?:[^./][^/]*/)?tools/shadercache/template\.bin$")

# Independently reviewed typed Vulkan whitelist; no opaque binary fields.
def _validate_vulkan_template(p, fields, number, array):
    VK_GROUPS = {'assembly':3,'viewport':3,'raster':11,'multisample':6,'depth':6,'front_stencil':7,'back_stencil':7,'depth_bounds':2,'blend':3,'blend_constants':4}
    fields(p,('backend','shader_hashes','key_words','dynamic_uniforms','descriptors','rendering','formats','bindings','attributes','state','attachments','dynamics'))
    if p['backend']!='vulkan': raise ValueError('pipeline backend')
    for v in array(p['shader_hashes'],3): number(v,high=0xffffffffffffffff)
    for v in array(p['key_words'],46): number(v)
    number(p['dynamic_uniforms'],0,1)
    for stage, bindings in enumerate(array(p['descriptors'],2)):
        seen=set()
        for d in array(bindings,maximum=128):
            array(d,4)
            for v in d: number(v)
            binding,typ,count,flags=d
            if binding>=256 or binding in seen or count!=1 or flags!=(16 if stage else 1) or typ not in (1,8 if p['dynamic_uniforms'] else 6): raise ValueError('invalid descriptor layout')
            seen.add(binding)
    for v in array(p['rendering'],3): number(v,0,184)
    if p['rendering'][0]: raise ValueError('multiview unsupported')
    for v in array(p['formats'],maximum=8): number(v,0,184)
    for d in array(p['bindings'],maximum=16):
        array(d,3); number(d[0],0,15);number(d[1],0,65535);number(d[2],0,1)
    for d in array(p['attributes'],maximum=64):
        array(d,4);number(d[0],0,63);number(d[1],0,15);number(d[2],1,184);number(d[3],0,65535)
    fields(p['state'],VK_GROUPS)
    for name,n in VK_GROUPS.items():
        for v in array(p['state'][name],n): number(v)
    for d in array(p['attachments'],len(p['formats'])):
        for v in array(d,8): number(v)
        if d[0]>1 or any(v>18 for v in (d[1],d[2],d[4],d[5])) or d[3]>4 or d[6]>4 or d[7]>15: raise ValueError('invalid blend attachment')
    for v in array(p['dynamics'],maximum=4): number(v,0,8)
    # Mirrors runtime schema's booleans/enums; floats stored as IEEE bit words.
    st=p['state']; ra=st['raster']; de=st['depth']; mu=st['multisample']; bl=st['blend']
    if st['assembly'][0] or st['assembly'][1]>5 or st['assembly'][2]>1 or st['viewport']!=[0,1,1] or ra[0] or ra[1]>1 or ra[2] or ra[3] or ra[4]>3 or ra[5]>1 or ra[6]>1 or ra[10]!=0x3f800000: raise ValueError('unsupported raster state')
    if de[0] or any(de[i]>1 for i in (1,2,4,5)) or de[3]>7 or de[4]: raise ValueError('unsupported depth state')
    if mu[0] or mu[1]!=1 or mu[2] or mu[4] or mu[5]: raise ValueError('unsupported multisample')
    if bl[0] or bl[1]>1 or bl[2]>15: raise ValueError('unsupported blend state')
    for name in ('front_stencil','back_stencil'):
        if any(v>7 for v in st[name][:4]): raise ValueError('invalid stencil enum')
    for values in (ra[7:11], [mu[3]], st['depth_bounds'], st['blend_constants']):
        if any(value & 0x7f800000 == 0x7f800000 for value in values):
            raise ValueError('nonfinite Vulkan fixed-state float')
    if p['dynamics'] != [0,1,4,8]:
        raise ValueError('unsupported Vulkan dynamic state')
    return p


def validate_code_free_template(data):
    if not data.startswith(SAFE_TEMPLATE_MAGIC) or len(data) > TEMPLATE_MAX_BYTES:
        raise ValueError("invalid code-free template envelope")
    dec = zlib.decompressobj()
    raw = dec.decompress(data[8:], TEMPLATE_MAX_BYTES + 1)
    if len(raw) > TEMPLATE_MAX_BYTES or dec.unconsumed_tail or not dec.eof or dec.unused_data:
        raise ValueError("invalid or trailing template compression")
    def unique(items):
        result = {}
        for k, v in items:
            if k in result:
                raise ValueError("duplicate template field")
            result[k] = v
        return result
    def fields(obj, names):
        if type(obj) is not dict or set(obj) != set(names):
            raise ValueError("unknown or missing template field")
    def number(value, low=0, high=0xffffffff):
        if type(value) is not int or not low <= value <= high:
            raise ValueError("invalid template integer")
    def array(value, size=None, maximum=100000):
        if type(value) is not list or len(value) > maximum or (size is not None and len(value) != size):
            raise ValueError("invalid template array")
        return value
    doc = json.loads(raw, object_pairs_hook=unique,
                     parse_constant=lambda value: (_ for _ in ()).throw(ValueError("nonfinite JSON")))
    fields(doc, ("version", "states", "pipelines"))
    number(doc["version"], 3, 3)
    attributes = ("buffer", "semantic", "format", "fetch_type", "numeric", "signed", "endian", "x", "y", "z", "w", "offset", "divisor")
    formats = {1,2,5,6,7,13,14,15,16,22,26,25,29,30,31,32,47,48,34,35}
    def fetch_description(fetch):
        fields(fetch, ("hash", "compact", "metal_hash", "attributes"))
        number(fetch["hash"], high=0xffffffffffffffff)
        number(fetch["compact"], 0, 1)
        number(fetch["metal_hash"], high=0xffffffffffffffff)
        for attr in array(fetch["attributes"], maximum=64):
            fields(attr, attributes)
            for name in attributes:
                number(attr[name], -1 if name == "divisor" else 0,
                       0x7fffffff if name == "divisor" else 0xffffffff)
            if (attr["buffer"] > 255 or attr["semantic"] > 255 or attr["format"] not in formats or
                attr["fetch_type"] > 1 or attr["numeric"] > 2 or attr["signed"] > 1 or attr["endian"] > 2 or
                attr["offset"] > 65535 or any(attr[n] > 7 for n in ("x", "y", "z", "w")) or
                (attr["fetch_type"] == 1 and attr["divisor"] < 1)):
                raise ValueError("unsupported fetch attribute")
    for state in array(doc["states"], maximum=TEMPLATE_MAX_ENTRIES):
        names = ("hash", "vertex", "fetch", "registers")
        fields(state, names + (("linked_vertex",) if type(state) is dict and "linked_vertex" in state else ()))
        number(state["hash"], high=0xffffffffffffffff)
        number(state["vertex"], 0, 1)
        fetch = state["fetch"]
        if fetch is not None:
            if not state["vertex"]:
                raise ValueError("pixel fetch descriptor")
            fetch_description(fetch)
        linked = state.get("linked_vertex")
        if linked is not None:
            if state["vertex"]:
                raise ValueError("vertex shader with linked vertex")
            fields(linked, ("hash", "fetch"))
            number(linked["hash"], high=0xffffffffffffffff)
            fetch_description(linked["fetch"])
        seen = set()
        for pair in array(state["registers"], maximum=16384):
            array(pair, 2)
            address, value = pair
            number(address, 0, 65535); number(value)
            if (address in seen or address in (0xa210, 0xa216, 0xa225) or
                not (0x2000 <= address < 0x4000 or 0xa000 <= address < 0xc000 or 0xe000 <= address < 0x10000)):
                raise ValueError("unapproved or duplicate register")
            seen.add(address)
    for pipeline in array(doc["pipelines"], maximum=TEMPLATE_MAX_ENTRIES):
        if type(pipeline) is dict and pipeline.get("backend") == "vulkan":
            _validate_vulkan_template(pipeline, fields, number, array)
            continue
        fields(pipeline, ("backend", "shader_hashes", "formats", "blend", "color_control", "target_mask", "strides"))
        if pipeline["backend"] != "metal":
            raise ValueError("unsupported pipeline backend")
        for value in array(pipeline["shader_hashes"], 3): number(value, high=0xffffffffffffffff)
        for value in array(pipeline["formats"], 9): number(value, 0, 0x8ff)
        for value in array(pipeline["blend"], 8): number(value)
        number(pipeline["color_control"]); number(pipeline["target_mask"])
        for value in array(pipeline["strides"], 16): number(value, 0, 65535)


def windows_manifests(data):
    """Read actual RT_MANIFEST resources, never accept XML merely appended to a PE."""
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError('truncated PE resource')
        return struct.unpack_from(fmt, data, offset)[0]
    u16 = lambda offset: unpack('<H', offset)
    u32 = lambda offset: unpack('<I', offset)
    if data[:2] != b'MZ': raise ValueError('not a Windows executable')
    pe = u32(0x3c)
    if data[pe:pe+4] != b'PE\0\0': raise ValueError('invalid PE signature')
    optional = pe + 24
    magic = u16(optional)
    if magic not in (0x10b, 0x20b): raise ValueError('unknown PE optional header')
    directory_offset = 112 if magic == 0x20b else 96
    if u16(pe+20) < directory_offset+24 or u32(optional+directory_offset-4) < 3:
        raise ValueError('truncated PE data directories')
    directory = optional + directory_offset
    resource_rva, resource_size = u32(directory+16), u32(directory+20)
    sections = optional + u16(pe+20)
    section_count = u16(pe+6)
    if not 0 < section_count <= 96 or not resource_rva or resource_size < 16:
        raise ValueError('missing PE resources')
    def file_offset(rva, size=1):
        for i in range(section_count):
            section = sections + 40*i
            va, raw_size, raw = u32(section+12), u32(section+16), u32(section+20)
            if va <= rva and rva-va+size <= raw_size:
                offset = raw+rva-va
                if offset+size <= len(data): return offset
        raise ValueError('unmapped PE resource')
    root = file_offset(resource_rva, resource_size)
    def entries(relative):
        if relative+16 > resource_size: raise ValueError('invalid resource directory')
        start = root+relative
        count = u16(start+12)+u16(start+14)
        if count > 4096 or relative+16+8*count > resource_size:
            raise ValueError('invalid resource entries')
        return [(u32(start+16+8*i), u32(start+20+8*i)) for i in range(count)]
    manifests = []
    for kind, type_child in entries(0):
        if kind != 24: continue
        if not type_child & 0x80000000: raise ValueError('invalid manifest type')
        for identifier, id_child in entries(type_child & 0x7fffffff):
            if identifier != 1 or not id_child & 0x80000000:
                raise ValueError('application manifest must use resource ID 1')
            for language, leaf in entries(id_child & 0x7fffffff):
                if leaf & 0x80000000 or leaf+16 > resource_size:
                    raise ValueError('invalid manifest data entry')
                rva, size = u32(root+leaf), u32(root+leaf+4)
                if not 0 < size <= 1024*1024: raise ValueError('invalid manifest size')
                offset = file_offset(rva, size)
                manifests.append(data[offset:offset+size].rstrip(b'\0'))
    if not manifests: raise ValueError('missing application manifest')
    return manifests


def check_windows_manifest(name, data, problems):
    try:
        for manifest in windows_manifests(data):
            if b'<!DOCTYPE' in manifest.upper(): raise ValueError('manifest DTD unsupported')
            document = ET.fromstring(manifest)
            if document.tag != '{urn:schemas-microsoft-com:asm.v1}assembly':
                raise ValueError('invalid application manifest root')
            nodes = document.findall('./{urn:schemas-microsoft-com:asm.v3}application/'
                '{urn:schemas-microsoft-com:asm.v3}windowsSettings/'
                '{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage')
            active = [node.text.strip() if node.text else '' for node in nodes]
            if active != ['UTF-8']: raise ValueError('application manifest must select activeCodePage UTF-8')
            long_paths = document.findall('./{urn:schemas-microsoft-com:asm.v3}application/'
                '{urn:schemas-microsoft-com:asm.v3}windowsSettings/'
                '{http://schemas.microsoft.com/SMI/2016/WindowsSettings}longPathAware')
            if [node.text.strip() if node.text else '' for node in long_paths] != ['true']:
                raise ValueError('application manifest must declare longPathAware true (issue #144)')
            if not any(node.tag.endswith('}requestedExecutionLevel') for node in document.iter()):
                raise ValueError('application manifest lacks requestedExecutionLevel')
    except (ValueError, struct.error, ET.ParseError) as error:
        problems.append('%s: invalid Windows application manifest: %s' % (name, error))


SEALED_TEMPLATE_MAGIC = b"WWHS0001"


def open_sealed_template(data, key):
    """The guard's own reader of a sealed template (shaderprep_format.seal): HMAC-SHA256 tag over magic + nonce +
    ciphertext, then the SHAKE-256 keystream of key + nonce. The key is the release's template key
    (WWHD_TEMPLATE_KEY, 64 hex digits: a hash of the game files the template is sealed for)."""
    import hmac
    if len(data) < 56 or len(data) > TEMPLATE_MAX_BYTES:
        raise ValueError("invalid sealed template envelope")
    nonce, tag, body = data[8:24], data[24:56], data[56:]
    if not hmac.compare_digest(tag, hmac.new(key, data[:8] + nonce + body, hashlib.sha256).digest()):
        raise ValueError("sealed template does not open with WWHD_TEMPLATE_KEY")
    stream = hashlib.shake_256(b"WWHD shader template stream 1\0" + key + nonce).digest(len(body))
    return bytes(a ^ b for a, b in zip(body, stream))


def check_entry(name, data, problems):
    n = name.replace("\\", "/")
    if n.rsplit("/", 1)[-1].lower() in ("wwhd.exe", "wind waker hd.exe"):
        check_windows_manifest(name, data, problems)
    safe_template = False
    if data.startswith(SEALED_TEMPLATE_MAGIC):
        # sealed (a key from the player's game files): checked as the code-free template it holds, or rejected
        try:
            key = os.environ.get("WWHD_TEMPLATE_KEY", "")
            if len(key) != 64:
                raise ValueError("a sealed template can only be checked with WWHD_TEMPLATE_KEY")
            validate_code_free_template(open_sealed_template(data, bytes.fromhex(key)))
            if not SAFE_TEMPLATE_PATH.fullmatch(n):
                raise ValueError("code-free template outside approved package path")
            safe_template = True
        except (ValueError, TypeError, OverflowError, RecursionError, zlib.error) as error:
            problems.append("%s: invalid sealed template: %s" % (name, error))
    elif data.startswith(SAFE_TEMPLATE_MAGIC):
        try:
            validate_code_free_template(data)
            if not SAFE_TEMPLATE_PATH.fullmatch(n):
                raise ValueError("code-free template outside approved package path")
            safe_template = True
        except (ValueError, TypeError, OverflowError, RecursionError, zlib.error) as error:
            problems.append("%s: invalid code-free template: %s" % (name, error))
    for rx, why in BAD_NAME:
        if rx.search(n) and not (safe_template and why in ("shader cache", "local shader cache tree")):
            problems.append("%s: %s" % (name, why))
    ext = os.path.splitext(n)[1].lower()
    if not safe_template and data.startswith((b"WWHT", b"WWVKSC", b"WWVKPC", b"WWVKPR", b"WWVKST", b"WWFA", b"\x03\x02\x23\x07")):
        problems.append("%s: shader template, Vulkan cache or SPIR-V content" % name)
    if len(data) >= 14:
        kind, raw, compressed = struct.unpack_from("<III", data)
        # Runtime recipe streams have no global magic. Identify the record
        # envelope plus a valid zlib header, even if a file has been renamed.
        zlib_header = int.from_bytes(data[12:14], "big")
        if kind in (1, 2, 3, 4, 5, 6) and 0 < raw <= 16 * 1024 * 1024 and \
                2 <= compressed <= len(data) - 12 and data[12] & 15 == 8 and zlib_header % 31 == 0:
            problems.append("%s: local shader recipe stream" % name)
    if ext in TEXT_EXT and b"\0" not in data[:8192]:
        for m in KEYLIKE.finditer(data):
            line = data.count(b"\n", 0, m.start()) + 1
            problems.append("%s:%d: 32-hex-digit string (key-like)" % (name, line))
    if GEN_CODE.search(data):
        problems.append("%s: contains recompiled game functions" % name)


ANDROID_TEST_FILES = {
    "tools/android/debug_fixture_build.py", "tools/android/service_fixture.py",
    "tools/android/embedding_smoke.py", "tools/recomp/android_fixture.py",
    "tools/android/extraction-fixture.bin", "tools/android/runtime_fixture.c",
}


RUNTIME_LICENSES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "appimage-runtime-licenses")


def check_appimage_licenses(path, problems):
    """An AppImage tree has the runtime's license texts, byte for byte (appimage.py add_runtime_licenses)."""
    names = sorted(f for f in os.listdir(RUNTIME_LICENSES) if f.startswith("AppImage-runtime-") and f.endswith(".txt"))
    if not names:
        problems.append("no AppImage runtime license texts in " + RUNTIME_LICENSES)
    for name in names:
        shipped = os.path.join(path, "third-party-licenses", name)
        if not os.path.isfile(shipped):
            problems.append("third-party-licenses/%s: the AppImage runtime's license text is missing" % name)
            continue
        with open(shipped, "rb") as a, open(os.path.join(RUNTIME_LICENSES, name), "rb") as b:
            if a.read() != b.read():
                problems.append("third-party-licenses/%s: differs from tools/release/appimage-runtime-licenses" % name)


def scan(path, allow_android_test_fixtures=False):
    problems, count, python = [], 0, set()
    files = python_files(path)

    def entry(name, data):
        check_python(name, data, python, problems, files)
        check_entry(name, data, problems)
        if name.replace("\\", "/").endswith("python-source.zip"):
            try:
                with zipfile.ZipFile(io.BytesIO(data)) as source:
                    forbidden = ANDROID_TEST_FILES.intersection(source.namelist())
                    if forbidden and not allow_android_test_fixtures:
                        problems.append("%s: debug-only Android resources: %s" % (name, ", ".join(sorted(forbidden))))
            except zipfile.BadZipFile:
                problems.append("%s: invalid Android Python source archive" % name)

    if os.path.isdir(path) and os.path.isfile(os.path.join(path, "AppRun")):
        check_appimage_licenses(path, problems)
    if os.path.isdir(path):
        for dp, _, fns in os.walk(path):
            for fn in fns:
                full = os.path.join(dp, fn)
                with open(full, "rb") as f:
                    entry(os.path.relpath(full, path), f.read())
                count += 1
    else:
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                entry(info.filename, z.read(info))
                count += 1
    if python:
        with open(files) as f:
            missing = sorted(set(json.load(f)["files"]) - python)
        if missing:
            problems.append("tools/python/ lacks files of the pinned embeddable Python: " + ", ".join(missing))
    return problems, count


def main():
    args = sys.argv[1:]
    allow_tests = "--allow-android-test-fixtures" in args
    if allow_tests: args.remove("--allow-android-test-fixtures")
    if not args:
        sys.exit(__doc__)
    bad = False
    for p in args:
        problems, count = scan(p, allow_android_test_fixtures=allow_tests)
        if problems:
            bad = True
            print("REJECTED %s (%d files):" % (p, count))
            for pr in problems:
                print("  " + pr)
        else:
            print("ok %s (%d files checked)" % (p, count))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
