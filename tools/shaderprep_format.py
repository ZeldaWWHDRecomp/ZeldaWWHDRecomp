"""Code-free WWHT0003 schema. Only hashes, semantic attributes and typed GPU state.

Local WWFA0001 fetch payload: magic, little-endian u64 source hash, u32
attribute count, u32 compact provenance (0 legacy/1 WWFS), u64 Metal hash,
then 13 words per attribute (last word signed divisor).
No executable instructions, shader source or arbitrary byte/string fields exist.
"""
import array
import math
import json
import struct
import zlib

MAGIC = b'WWHT0003'
FETCH_MAGIC = b'WWFA0001'
# A full-game template (about 90,000 states, 40,000 pipelines) decodes to about 300 MiB of JSON.
# tools/release/guard.py keeps its own copy of these limits; change both together.
MAX_BYTES = 1024 * 1024 * 1024  # decoded JSON
MAX_ENTRIES = 1000000           # states, and pipelines
ATTR_FIELDS = ('buffer', 'semantic', 'format', 'fetch_type', 'numeric', 'signed',
               'endian', 'x', 'y', 'z', 'w', 'offset', 'divisor')
RAW_FORMATS = (1,2,5,6,7,13,14,15,16,22,26,25,29,30,31,32,47,48,34,35)
FORMATS = set(RAW_FORMATS)

def integer(v, lo=0, hi=0xffffffff):
    if type(v) is not int or not lo <= v <= hi:
        raise ValueError('integer outside schema')
    return v

def keys(obj, names):
    if type(obj) is not dict or set(obj) != set(names):
        raise ValueError('unknown or missing schema field')

def vector(v, count=None, maximum=65536):
    if type(v) is not list or len(v)>maximum or (count is not None and len(v)!=count):
        raise ValueError('invalid schema vector')
    return v

def validate_fetch(f):
    keys(f, ('hash','metal_hash','compact','attributes'))
    integer(f['hash'],hi=0xffffffffffffffff)
    integer(f['metal_hash'],hi=0xffffffffffffffff)
    integer(f['compact'],0,1)
    for a in vector(f['attributes'],maximum=64):
        keys(a, ATTR_FIELDS)
        for name in ATTR_FIELDS: integer(a[name], -1 if name=='divisor' else 0,
                                      0x7fffffff if name=='divisor' else 0xffffffff)
        if a['buffer']>255 or a['semantic']>255 or a['format'] not in FORMATS or a['fetch_type']>1 or a['numeric']>2 or a['signed']>1 or a['endian']>2 or a['offset']>65535 or any(a[n]>7 for n in ('x','y','z','w')):
            raise ValueError('unsupported fetch attribute')
        if a['fetch_type']==1 and a['divisor']<1:
            raise ValueError('invalid instance divisor')
    return f

def fetch_payload(f):
    validate_fetch(f)
    return FETCH_MAGIC+struct.pack('<QIIQ',f['hash'],len(f['attributes']),f['compact'],f['metal_hash'])+b''.join(
        struct.pack('<12Ii',*(a[n] for n in ATTR_FIELDS)) for a in f['attributes'])

def metal_fetch_hash(data):
    """Metal hash_bytes(raw fetch), before its separate stride fold."""
    mask=0xffffffffffffffff
    h=0x9e3779b97f4a7c15
    at=0
    while at+8<=len(data):
        word=int.from_bytes(data[at:at+8],'little')
        h=((h^word)*0xff51afd7ed558ccd)&mask
        h^=h>>32;at+=8
    for v in data[at:]: h=((h^v)*0x100000001b3)&mask
    return h^(h>>29)


def parse_fetch(data, regs, program_hash):
    if not data: return None
    r=dict(regs)
    if data.startswith(FETCH_MAGIC):
        if len(data)<32: raise ValueError('truncated semantic fetch')
        h,n,compact,mh=struct.unpack_from('<QIIQ',data,8)
        if n>64 or len(data)!=32+52*n: raise ValueError('invalid semantic fetch length')
        return validate_fetch({'hash':h,'metal_hash':mh,'compact':compact,'attributes':[dict(zip(ATTR_FIELDS,struct.unpack_from('<12Ii',data,32+52*i))) for i in range(n)]})
    attrs=[]
    if data.startswith(b'WWFS'):
        if len(data)<16: raise ValueError('truncated compact fetch')
        n,res0,res1=struct.unpack_from('>III',data,4)
        if n>64 or len(data)!=16+n*16 or res0 or res1: raise ValueError('invalid compact fetch')
        for i in range(n):
            w0,offset,w2,ds=struct.unpack_from('>4I',data,16+i*16)
            fmt=w2&65535; raw=fmt&63; typ=(w0>>16)&255; endian=w0>>24
            if raw>=len(RAW_FORMATS) or typ>1 or endian>3 or fmt&~0xb3f or ds&0xf8f8f8f8:
                raise ValueError('unsupported compact fetch feature')
            if endian==3: endian=0 if raw in (0,1,4,10) else 1 if raw in (2,3,7,8,14,15) else 2
            vals=(w0>>8&255,w0&255,RAW_FORMATS[raw],typ,2 if fmt&0x800 else 1 if fmt&0x100 else 0, bool(fmt&0x200),endian,*(ds>>s&7 for s in (24,16,8,0)),offset,max(w2>>16,1) if typ else 0)
            attrs.append(dict(zip(ATTR_FIELDS,map(int,vals))))
        attrs = sorted((a for a in attrs if a['buffer']<16), key=lambda a:a['buffer'])
    else:
        if len(data)%16 or len(data)>4096: raise ValueError('invalid legacy fetch length')
        words=struct.unpack('<%dI'%(len(data)//4),data)
        clauses=[]
        if len(words)>=2 and words[0]&1==0 and words[0]<=0x30 and words[1]&~((3<<10)|(1<<19))==0x01800000:
            terminated=False
            for at in range(0,len(words),2):
                w0,w1=words[at:at+2]; op=w1>>23&127
                if op==20:
                    if w0 or w1&~((127<<23)|(1<<31)): raise ValueError('unsupported fetch return')
                    terminated=True;break
                if op!=3 or w1&~((127<<23)|(7<<10)|(1<<19)|(1<<31)):
                    raise ValueError('unsupported fetch control flow')
                start=w0*8; count=((w1>>10&7)|(w1>>16&8))+1
                if start%16 or start+count*16>len(data): raise ValueError('fetch clause outside payload')
                clauses.extend(range(start//4,start//4+count*4,4))
            if not terminated: raise ValueError('unterminated fetch control flow')
        else:
            clauses=list(range(0,len(words),4))
        for at in clauses:
            w0,w1,w2,w3=words[at:at+4]
            # Legacy renderer intentionally ignores the MEM/clause header.
            # This is not executable replay; reproduce its parsed semantics.
            if w0&31==2 and len(clauses)==len(words)//4: continue
            if w0&31!=1: raise ValueError('unsupported legacy fetch opcode')
            # Match the renderer's semantic parser. Reject unrepresented source
            # addressing/constant-field/relative features rather than dropping them.
            if (w0>>16&127) or w0&(1<<23) or w1&(1<<21) or w2&(1<<18) or w3:
                raise ValueError('unsupported legacy fetch addressing')
            typ=w0>>5&3; src=w0>>24&3
            divisor=(-1, r.get(0xa2a8,0),r.get(0xa2a9,0),1)[src]
            vals=((w0>>8&255)-160,w1&255,w1>>22&63,typ,w1>>28&3,w1>>30&1,w2>>16&3,*(w1>>s&7 for s in (9,12,15,18)),w2&65535,divisor)
            attrs.append(dict(zip(ATTR_FIELDS,vals)))
    return validate_fetch({'hash':metal_fetch_hash(data),'metal_hash':metal_fetch_hash(data),'compact':int(data.startswith(b'WWFS')),'attributes':attrs})

STATE_KEYS = frozenset(('hash','vertex','fetch','registers'))

def validate_state(s):
    keys(s,('hash','vertex','fetch','registers') + (('linked_vertex',) if 'linked_vertex' in s else ()))
    linked=s.get('linked_vertex')
    if linked is not None:
        keys(linked,('hash','fetch'));integer(linked['hash'],hi=0xffffffffffffffff)
        validate_fetch(linked['fetch'])
        if s['vertex']: raise ValueError('vertex companion on vertex state')
    integer(s['hash'],hi=0xffffffffffffffff);integer(s['vertex'],0,1)
    if s['fetch'] is not None: validate_fetch(s['fetch'])
    if not s['vertex'] and s['fetch'] is not None: raise ValueError('pixel fetch state')
    seen=set()
    for pair in vector(s['registers'],maximum=16384):
        vector(pair,2); a,v=pair;integer(a,0,65535);integer(v)
        if a in seen or a in (0xa210,0xa216,0xa225) or not (0x2000<=a<0x4000 or 0xa000<=a<0xc000 or 0xe000<=a<0x10000):
            raise ValueError('invalid/duplicate/pointer register')
        seen.add(a)
    return s

class CompactState(tuple):
    """A validated state as decode(compact=True) keeps it: (hash, vertex, fetch, registers, linked_vertex),
    registers as Registers (8 bytes a pair instead of ~130 for JSON lists)."""
    __slots__ = ()

class Registers:
    """(register, value) pairs in one array; iterates as 2-tuples, so dict(Registers) works."""
    __slots__ = ('words',)
    def __init__(self, pairs):
        self.words = array.array('I')
        for a, v in pairs: self.words.append(a); self.words.append(v)
    def __len__(self): return len(self.words) // 2
    def __iter__(self):
        it = iter(self.words)
        return zip(it, it)
    def __eq__(self, other): return isinstance(other, Registers) and self.words == other.words
    __hash__ = None
    def __repr__(self): return 'Registers(%r)' % list(self)

def validate_document(doc):
    keys(doc,('version','states','pipelines'))
    if doc['version']!=3: raise ValueError('unsupported template version')
    for s in vector(doc['states'],maximum=MAX_ENTRIES):
        if type(s) is not CompactState: validate_state(s)  # compact ones were validated while decoding
    for p in vector(doc['pipelines'],maximum=MAX_ENTRIES):
        if type(p) is dict and p.get('backend')=='vulkan':
            validate_vulkan(p);continue
        keys(p,('backend','shader_hashes','formats','blend','color_control','target_mask','strides'))
        if p['backend']!='metal': raise ValueError('unsupported pipeline descriptor backend')
        for h in vector(p['shader_hashes'],3): integer(h,hi=0xffffffffffffffff)
        for v in vector(p['formats'],9): integer(v,0,0x8ff)
        for v in vector(p['blend'],8): integer(v)
        integer(p['color_control']);integer(p['target_mask'])
        for v in vector(p['strides'],16): integer(v,0,65535)
    return doc

def encode(doc):
    validate_document(doc)
    return MAGIC+zlib.compress(json.dumps(doc,sort_keys=True,separators=(',',':')).encode('ascii'),9)

def decode(data, compact=False):
    """compact: states come back as CompactState tuples (validated), see pack below."""
    if not data.startswith(MAGIC): raise ValueError('not a code-free template')
    dec=zlib.decompressobj();raw=dec.decompress(data[8:],MAX_BYTES+1)
    if len(raw)>MAX_BYTES or dec.unconsumed_tail or not dec.eof or dec.unused_data: raise ValueError('invalid template compression')
    def unique(items):
        result={}
        for k,v in items:
            if k in result: raise ValueError('duplicate JSON field')
            result[k]=v
        return result
    if not compact:
        return validate_document(json.loads(raw,object_pairs_hook=unique))
    # Each state is validated and packed as soon as the parser finishes it, so a full-game template
    # (about 15 million register pairs) never exists as JSON lists all at once.
    def pack(items):
        obj=unique(items)
        if STATE_KEYS <= obj.keys() and obj.keys() <= STATE_KEYS | {'linked_vertex'}:
            validate_state(obj)
            return CompactState((obj['hash'],obj['vertex'],obj['fetch'],Registers(obj['registers']),obj.get('linked_vertex')))
        return obj
    del dec
    doc=json.loads(raw,object_pairs_hook=pack)
    return validate_document(doc)

def metal_pipeline(raw):
    if len(raw)!=168 or raw[164:]!=b'\0'*4: raise ValueError('unsupported Metal pipeline layout/padding')
    v=struct.unpack('<3Q35I',raw[:164])
    return {'backend':'metal','shader_hashes':list(v[:3]),'formats':list(v[3:12]),'blend':list(v[12:20]),'color_control':v[20],'target_mask':v[21],'strides':[(x>>11)&65535 for x in v[22:38]]}

def metal_payload(p):
    validate_document({'version':3,'states':[],'pipelines':[p]})
    return struct.pack('<3Q35I',*p['shader_hashes'],*p['formats'],*p['blend'],p['color_control'],p['target_mask'],*(x<<11 for x in p['strides']))+b'\0'*4

# Exactly Parts::visit, named groups exclude handles/pointers and instruction data.
VK_GROUPS = {'assembly':3,'viewport':3,'raster':11,'multisample':6,
             'depth':6,'front_stencil':7,'back_stencil':7,'depth_bounds':2,
             'blend':3,'blend_constants':4}

def validate_vulkan(p):
    keys(p,('backend','shader_hashes','key_words','dynamic_uniforms','descriptors','rendering','formats','bindings','attributes','state','attachments','dynamics'))
    if p['backend']!='vulkan': raise ValueError('pipeline backend')
    for v in vector(p['shader_hashes'],3): integer(v,hi=0xffffffffffffffff)
    for v in vector(p['key_words'],46): integer(v)
    integer(p['dynamic_uniforms'],0,1)
    for stage, bindings in enumerate(vector(p['descriptors'],2)):
        seen=set()
        for d in vector(bindings,maximum=128):
            vector(d,4)
            for v in d: integer(v)
            binding,typ,count,flags=d
            if binding>=256 or binding in seen or count!=1 or flags!=(16 if stage else 1) or typ not in (1,8 if p['dynamic_uniforms'] else 6): raise ValueError('invalid descriptor layout')
            seen.add(binding)
    for v in vector(p['rendering'],3): integer(v,0,184)
    if p['rendering'][0]: raise ValueError('multiview unsupported')
    for v in vector(p['formats'],maximum=8): integer(v,0,184)
    for d in vector(p['bindings'],maximum=16):
        vector(d,3); integer(d[0],0,15);integer(d[1],0,65535);integer(d[2],0,1)
    for d in vector(p['attributes'],maximum=64):
        vector(d,4);integer(d[0],0,63);integer(d[1],0,15);integer(d[2],1,184);integer(d[3],0,65535)
    keys(p['state'],VK_GROUPS)
    for name,n in VK_GROUPS.items():
        for v in vector(p['state'][name],n): integer(v)
    for d in vector(p['attachments'],len(p['formats'])):
        for v in vector(d,8): integer(v)
        if d[0]>1 or any(v>18 for v in (d[1],d[2],d[4],d[5])) or d[3]>4 or d[6]>4 or d[7]>15: raise ValueError('invalid blend attachment')
    for v in vector(p['dynamics'],maximum=4): integer(v,0,8)
    if p['dynamics'] != [0,1,4,8]: raise ValueError('unsupported dynamic state')
    # Mirrors runtime schema's booleans/enums; floats stored as IEEE bit words.
    st=p['state']; ra=st['raster']; de=st['depth']; mu=st['multisample']; bl=st['blend']
    if st['assembly'][0] or st['assembly'][1]>5 or st['assembly'][2]>1 or st['viewport']!=[0,1,1] or ra[0] or ra[1]>1 or ra[2] or ra[3] or ra[4]>3 or ra[5]>1 or ra[6]>1 or ra[10]!=0x3f800000: raise ValueError('unsupported raster state')
    if de[0] or any(de[i]>1 for i in (1,2,4,5)) or de[3]>7 or de[4]: raise ValueError('unsupported depth state')
    if mu[0] or mu[1]!=1 or mu[2] or mu[4] or mu[5]: raise ValueError('unsupported multisample')
    if bl[0] or bl[1]>1 or bl[2]>15: raise ValueError('unsupported blend state')
    for name,indices in (('raster',(7,8,9,10)),('multisample',(3,)),('depth_bounds',(0,1)),('blend_constants',(0,1,2,3))):
        if any(not math.isfinite(struct.unpack('<f',struct.pack('<I',st[name][i]))[0]) for i in indices):
            raise ValueError('nonfinite pipeline state')
    for name in ('front_stencil','back_stencil'):
        if any(v>7 for v in st[name][:4]): raise ValueError('invalid stencil enum')
    return p

class Words:
    def __init__(self,data):
        if len(data)%4: raise ValueError('unaligned recipe')
        self.words=list(struct.unpack('<%dI'%(len(data)//4),data));self.at=0
    def take(self,n):
        if n>len(self.words)-self.at: raise ValueError('truncated recipe')
        out=self.words[self.at:self.at+n];self.at+=n;return out
    def scalar(self): return self.take(1)[0]
    def vec(self,max_count):
        n=self.scalar()
        if n>max_count: raise ValueError('recipe count')
        return self.take(n)

def vulkan_recipe(data, codefree=False, shaders=None):
    """One recipe record. shaders: the shader count of a version 2 database ("WWVKPR02"), whose records hold the
    key as raw bytes, a shader index per stage instead of the SPIR-V, and the recipe's archive groups at the end."""
    w=Words(data)
    if shaders is not None:
        key=struct.pack('<52I',*w.take(52))
    else:
        key=w.vec(208)
        if len(key)!=208 or any(v>255 for v in key): raise ValueError('pipeline key schema')
        key=bytes(key)
    p={'backend':'vulkan','shader_hashes':list(struct.unpack_from('<3Q',key)),
       'key_words':list(struct.unpack_from('<46I',key,24)), 'dynamic_uniforms':w.scalar()}
    p['descriptors']=[]
    for _ in range(2):
        if shaders is not None:
            if w.scalar()>=shaders: raise ValueError('recipe shader index')
            code=None
        else: code=w.vec(262144)
        if code is None: pass
        elif codefree:
            if code: raise ValueError('code in stripped Vulkan recipe')
        elif len(code)<6 or code[0]!=0x07230203: raise ValueError('invalid local SPIR-V')
        n=w.scalar()
        if n>128: raise ValueError('descriptor count')
        p['descriptors'].append([w.take(4) for _ in range(n)])
    p['rendering']=w.take(3);p['formats']=w.vec(8)
    n=w.scalar()
    if n>16: raise ValueError('binding count')
    p['bindings']=[w.take(3) for _ in range(n)]
    n=w.scalar()
    if n>64: raise ValueError('attribute count')
    p['attributes']=[w.take(4) for _ in range(n)]
    p['state']={k:w.take(n) for k,n in VK_GROUPS.items()}
    n=w.scalar()
    if n>8: raise ValueError('attachment count')
    p['attachments']=[w.take(8) for _ in range(n)];p['dynamics']=w.vec(4)
    if shaders is not None: w.vec(8)  # archive groups: local prefetch hints, not part of the pipeline
    if w.at!=len(w.words): raise ValueError('trailing recipe')
    return validate_vulkan(p)

VK_DATABASE_MAX = 16384

def vulkan_database_shaders(data):
    """(shader count or None, offset of the first recipe) of a Vulkan recipe database: version 2 ("WWVKPR02", the
    runtime's local database) starts with its shader table (words, packed size, digest, zlib bytes padded to 4)."""
    if data[:8]==b'WWVKPR01': return None,64
    if data[:8]!=b'WWVKPR02': raise ValueError('unsupported Vulkan database')
    if len(data)<68: raise ValueError('truncated database')
    n=struct.unpack_from('<I',data,64)[0];at=68
    if n>2*VK_DATABASE_MAX: raise ValueError('shader count')
    for _ in range(n):
        if at+16>len(data): raise ValueError('truncated database')
        size=struct.unpack_from('<I',data,at+4)[0];at+=16+((size+3)&~3)
        if at>len(data): raise ValueError('truncated database')
    return n,at

def vulkan_database(data):
    if len(data)<64 or data[:8] not in (b'WWVKPR01',b'WWVKPR02'): raise ValueError('unsupported Vulkan database')
    n,crc=struct.unpack_from('<II',data,56)
    # runtime kPipelineRecipeMaxCount (the first recipe databases allowed 512)
    if n>VK_DATABASE_MAX or zlib.crc32(data[64:])!=crc: raise ValueError('invalid Vulkan database')
    shaders,at=vulkan_database_shaders(data);out=[]
    for _ in range(n):
        if at+4>len(data): raise ValueError('truncated database')
        size=struct.unpack_from('<I',data,at)[0];at+=4
        if size>4*1024*1024 or size>len(data)-at: raise ValueError('recipe bounds')
        out.append(vulkan_recipe(data[at:at+size],shaders=shaders));at+=size
    if at!=len(data): raise ValueError('trailing database')
    return out

def vulkan_payload(p):
    validate_vulkan(p)
    key=struct.pack('<3Q46I',*p['shader_hashes'],*p['key_words'])
    words=[208,*key,p['dynamic_uniforms']]
    for ds in p['descriptors']:
        words.extend([0,len(ds)])
        for d in ds: words.extend(d)
    words.extend(p['rendering']);words.extend([len(p['formats']),*p['formats']])
    for name in ('bindings','attributes'):
        words.append(len(p[name]))
        for d in p[name]: words.extend(d)
    for name in VK_GROUPS: words.extend(p['state'][name])
    words.append(len(p['attachments']))
    for d in p['attachments']: words.extend(d)
    words.extend([len(p['dynamics']),*p['dynamics']])
    return b'WWVKST01'+struct.pack('<%dI'%len(words),*words)

# ---------------------------------------------------------------- sealed template
# A release template can be sealed with a key only the player's own game files give: SHA-256 over three of the game's
# shader archives (the same in the USA and European games). Not secrecy against attackers (anyone with the game
# opens it); the shipped file only means something next to the game it belongs to. Layout: SEAL_MAGIC, a 16-byte
# nonce, a 32-byte HMAC-SHA256 tag over magic + nonce + ciphertext (a wrong game is reported, not mis-read), the
# ciphertext (the WWHT0003 bytes XOR a SHAKE-256 keystream of key + nonce). The nonce is derived from the key and
# the content, so sealing is deterministic. Idea after SwitchWakerHD's sealed shader manifest
# (thanks @Pulpparty / @depende3000); this is its own design.
SEAL_MAGIC = b'WWHS0001'
SEAL_KEY_FILES = ('Common/Shaders/Prim/wii_pipeline.sharcfb', 'Common/Shaders/particle/wii_pipeline.sharcfb',
                  'Common/Shaders/render_buffer/render_buffer.sharcfb')

def game_key(content):
    """The sealing key from the game's content folder; ValueError when a key file is missing."""
    import hashlib, os
    if os.path.isdir(os.path.join(content, 'content')): content = os.path.join(content, 'content')  # the game folder
    h = hashlib.sha256(b'WWHD shader template key 1\0')
    for rel in SEAL_KEY_FILES:
        path = os.path.join(content, *rel.split('/'))
        try:
            with open(path, 'rb') as f: digest = hashlib.sha256(f.read()).digest()
        except OSError:
            raise ValueError('game file missing for the shader template key: %s' % rel)
        h.update(rel.encode('ascii') + b'\0' + digest)
    return h.digest()

def _keystream(key, nonce, size):
    import hashlib
    return hashlib.shake_256(b'WWHD shader template stream 1\0' + key + nonce).digest(size)

def _xor(data, stream):
    return (int.from_bytes(data, 'little') ^ int.from_bytes(stream, 'little')).to_bytes(len(data), 'little')

def seal(plain, key):
    import hashlib, hmac
    if not plain.startswith(MAGIC): raise ValueError('only a code-free template can be sealed')
    nonce = hmac.new(key, b'nonce\0' + plain, hashlib.sha256).digest()[:16]
    body = _xor(plain, _keystream(key, nonce, len(plain)))
    tag = hmac.new(key, SEAL_MAGIC + nonce + body, hashlib.sha256).digest()
    return SEAL_MAGIC + nonce + tag + body

def is_sealed(data):
    return data.startswith(SEAL_MAGIC)

def open_sealed(data, key):
    import hashlib, hmac
    if not data.startswith(SEAL_MAGIC) or len(data) < 56 or len(data) > MAX_BYTES:
        raise ValueError('not a sealed shader template')
    nonce, tag, body = data[8:24], data[24:56], data[56:]
    if not hmac.compare_digest(tag, hmac.new(key, SEAL_MAGIC + nonce + body, hashlib.sha256).digest()):
        raise ValueError('the shader template does not belong to these game files')
    plain = _xor(body, _keystream(key, nonce, len(body)))
    if not plain.startswith(MAGIC): raise ValueError('sealed shader template holds no code-free template')
    return plain
