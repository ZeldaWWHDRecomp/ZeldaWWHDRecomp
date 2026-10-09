"""Curated ABI facts checked against explicit public declarations/register adapters."""
import re
from public_sdk_layouts import body_of

ENUMS = {
    'daFm_c': 'wwhd_src/include/d/actor/d_a_fm.h',
    'daNpc_So_c': 'wwhd_src/include/d/actor/d_a_npc_so.h',
    'daRd_c': 'wwhd_src/include/d/actor/d_a_rd.h',
    'daTag_Gship_c': 'wwhd_src/d/actor/d_a_tag_ghostship.cpp',
}


def aliases(root):
    result = {}
    for owner, source in ENUMS.items():
        body, _ = body_of((root / source).read_text(), owner)
        match = re.search(r'enum\s+Proc_e\s*\{([^{}]+)\}', body)
        if not match:
            raise ValueError('public enum declaration changed: ' + owner)
        values = re.sub(r'\s+', '', match[1]).rstrip(',')
        if values not in ('PROC_INIT_e=0,PROC_EXEC_e=1', 'PROC_INIT_e,PROC_RUN_e'):
            raise ValueError('public enum values changed: ' + owner)
        result[owner + '::Proc_e'] = 's32'  # ordinary 32-bit enum parameter ABI
    gabi = re.sub(r'\s+', '', (root / 'tools/verify/include/gabi.h').read_text())
    if 'structPair32{u32r3,r4;};' not in gabi or 'c->r[3]=r.r3;c->r[4]=r.r4;' not in gabi:
        raise ValueError('public Pair32 register ABI changed')
    xyz = re.sub(r'\s+', '', (root / 'wwhd_src/SSystem/SComponent/c_sxyz.cpp').read_text())
    if 'c->r[3]=value.xy;c->r[4]=u32(value.z)<<16;' not in xyz:
        raise ValueError('public six-byte vector return ABI changed')
    # A PowerPC unsigned-long-long result uses r3 (high word), r4 (low word).
    # Expose these explicit register results, never a C struct with a hidden result pointer.
    result['Pair32'] = 'wwhd_gpr_pair'
    result['SxyzResult'] = 'wwhd_gpr_pair'
    return result
