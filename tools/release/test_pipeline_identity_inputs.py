"""The pipeline runtime identity's inputs (cmake/pipeline_identity_inputs.txt) cannot go stale silently.

1. Every source of the runtime that creates pipelines or shaders, or names a shader or pipeline identity, is
   in the list (a text check, no build needed).
2. With a configured build (WWHD_BUILD_DIR, or build/ / build/rel with compile_commands.json): every runtime
   header that a translation unit creating pipelines or shaders includes, directly or not, is in the list (the
   compiler's own dependency list, -MM). Skipped without such a build.

usage: python3 tools/release/test_pipeline_identity_inputs.py [-v]   (WWHD_BUILD_DIR=<build> for part 2)
"""
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
LIST = ROOT / "cmake/pipeline_identity_inputs.txt"
SOURCE_EXT = (".c", ".cpp", ".h", ".hpp", ".inc", ".mm")
# what creates pipelines or shaders, or computes their identities: such a file must be covered
PIPELINE_CODE = re.compile(
    r"vkCreateGraphicsPipelines|vkCreateComputePipelines|vkCreateShaderModule|newRenderPipelineState|"
    r"newComputePipelineState|MTLRenderPipelineDescriptor|newLibraryWithSource|#version [0-9]{3}|glslang_|"
    r"glslang::|EShLang|pipelineId|PipelineRecipe|portable_output_identity|shader_interface\.h|"
    r"ShaderOverride|shader_override")


def entries():
    out = []
    for line in LIST.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.append(line)
    return out


def covered(rel, listed=None):
    rel = Path(rel).as_posix()
    for e in listed or entries():
        if rel == e or rel.startswith(e.rstrip("/") + "/"):
            return True
    return False


def runtime_sources():
    for p in sorted((ROOT / "runtime/src").rglob("*")):
        if p.is_file() and p.suffix in SOURCE_EXT:
            yield p


def build_dir():
    env = os.environ.get("WWHD_BUILD_DIR")
    candidates = [Path(env)] if env else [ROOT / "build", ROOT / "build/rel"]
    for c in candidates:
        if (c / "compile_commands.json").is_file():
            return c
    return None


def dependencies(entry):
    """The files the compiler reads for one compile_commands entry (-MM: no system headers)."""
    args = entry.get("arguments") or shlex.split(entry["command"])
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-MD", "-MMD") or a.startswith("-o"):
            continue
        out.append(a)
    p = subprocess.run(out + ["-MM", "-MF", "-"], cwd=entry["directory"], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if p.returncode != 0:
        raise RuntimeError("dependency scan of %s failed: %s" % (entry["file"], p.stderr.decode()[-2000:]))
    text = p.stdout.decode().replace("\\\n", " ")
    deps = text.split(":", 1)[1].split() if ":" in text else []
    return [os.path.normpath(os.path.join(entry["directory"], d)) for d in deps]


class PipelineIdentityInputs(unittest.TestCase):
    def test_list_entries_exist(self):
        for e in entries():
            self.assertTrue((ROOT / e).exists(), "listed but missing: %s" % e)

    def test_pipeline_and_shader_code_is_covered(self):
        listed = entries()
        missing = []
        for p in runtime_sources():
            rel = p.relative_to(ROOT).as_posix()
            if not covered(rel, listed) and PIPELINE_CODE.search(p.read_text(errors="replace")):
                missing.append(rel)
        self.assertEqual(missing, [], "these create or name pipelines/shaders; add them to %s" % LIST.relative_to(ROOT))

    def test_pipeline_units_include_only_listed_runtime_headers(self):
        if os.name == "nt":
            self.skipTest("the dependency scan runs on the Unix builds (the same sources)")
        build = build_dir()
        if not build:
            self.skipTest("no build with compile_commands.json (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)")
        listed = entries()
        commands = json.loads((build / "compile_commands.json").read_text())
        seen, missing = set(), {}
        for entry in commands:
            path = Path(os.path.normpath(os.path.join(entry["directory"], entry["file"])))
            try:
                rel = path.relative_to(ROOT).as_posix()
            except ValueError:
                continue
            if not (rel.startswith("runtime/src/") or rel.startswith("runtime/third_party/cemu/")) or rel in seen:
                continue
            seen.add(rel)
            if not PIPELINE_CODE.search(path.read_text(errors="replace")):
                continue
            for dep in dependencies(entry):
                try:
                    drel = Path(dep).resolve().relative_to(ROOT).as_posix()
                except ValueError:
                    continue  # system and generated headers
                if drel.startswith("runtime/src/") or drel.startswith("runtime/include/"):
                    if not covered(drel, listed):
                        missing.setdefault(drel, rel)
        self.assertTrue(seen, "no listed translation unit in %s" % build)
        self.assertEqual(missing, {}, "runtime headers included by listed units (header: first unit) but not "
                                      "listed in %s" % LIST.relative_to(ROOT))


if __name__ == "__main__":
    unittest.main()
