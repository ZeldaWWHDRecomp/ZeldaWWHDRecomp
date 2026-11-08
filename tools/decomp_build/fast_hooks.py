"""Decomp build, fast mode only (cmake WWHD_DECOMP_FAST, gen.py --hooked-bodies): port instruction hooks moved into the
decompiled source at build time.

A recompiled function with an instruction hook inside (tools/recomp/hooks*.txt `@ADDR`) has to stay recompiled in the
decomp build (recomp.py: "contains instruction hook"), and every call it makes into decompiled code is a guest entry.
For the hooks below the decompiled function offers the same point in its own source: gen.py rewrites that source in
the build directory (glued/ copy; the decompilation's source and what its verifier checks are unchanged) so the
decompiled body calls the port's hook there, and lists the site as `# moved-site ADDR` in decomp_hooks.txt, which
recomp.py then leaves out when it decides what to keep. The recompiled original (the slot's f_X_orig) still calls the
site, so WWHD_DECOMP_EXCLUDE=ADDR runs the old path.

Each entry: func (the decompiled function), site (the instruction hook), path (source, relative to the decompilation),
decl (inserted at the top of that source), old (text that occurs exactly once in the source) and new (its
replacement). A rewrite that does not match exactly once stops gen.py.
"""

HOOKS = [
    # fog_pin.cpp: 027FDA54 copies the fog object's near, far and colour; the reading thread's own fog record is
    # read instead (fog_pin_object: the object or this thread's private copy), right after the lookup and the
    # type / enabled checks, where the original's site 027FDF78 points r31 at the copy
    dict(func=0x027FDA54, site=0x027FDF78, path="wwhd_src/JSystem/J3DGraph/RenderFrameEnvironment.cpp",
         decl='extern "C" unsigned fog_pin_object(unsigned object);  /* the port (runtime/src/fog_pin.cpp), fast mode */\n',
         old="auto* fog = reinterpret_cast<RenderFogEnvironment_l*>(object);",
         new="auto* fog = gabi::at<RenderFogEnvironment_l>(fog_pin_object(gabi::ea(object)));  /* port hook (fog_pin) */"),
    # fog_pin.cpp: 0255F668 (GxFogSet_Sub, the one fog setter) records, per thread, the object and the converted colour
    # it stores (start and end it reads from the environment light itself), where the original's site 0255F7F8 sits:
    # at its first store into the object
    dict(func=0x0255F668, site=0x0255F7F8, path="wwhd_src/d/d_kankyo_5.cpp",
         decl='extern "C" void fog_pin_record(unsigned object, unsigned r, unsigned g, unsigned b, unsigned a);  /* the port (runtime/src/fog_pin.cpp), fast mode */\n',
         old="    // HD: upload the converted color and range into the shader parameter object.\n",
         new="    fog_pin_record(gabi::ea(object), colorFogWordBits(color.r), colorFogWordBits(color.g), colorFogWordBits(color.b),\n"
             "                   colorFogWordBits(color.a));  /* port hook (fog_pin) */\n"
             "    // HD: upload the converted color and range into the shader parameter object.\n"),
]


def apply(path, text):
    """TEXT of source PATH with the hooks for it applied; (text, [hooks applied])"""
    done = []
    for h in HOOKS:
        if h["path"] != path:
            continue
        n = text.count(h["old"])
        if n != 1:
            raise SystemExit("fast_hooks.py: %s: the hook text for site %08X occurs %d times (want 1)" % (path, h["site"], n))
        text = text.replace(h["old"], h["new"])
        done.append(h)
    if done:
        decls = "".join(sorted({h["decl"] for h in done}))
        # after the source's leading comment and includes: before its first non-preprocessor, non-comment line
        lines = text.split("\n")
        i = 0
        while i < len(lines) and (lines[i].startswith("#") or lines[i].startswith("//") or not lines[i].strip()):
            i += 1
        text = "\n".join(lines[:i] + [decls.rstrip("\n")] + lines[i:])
    return text, done
