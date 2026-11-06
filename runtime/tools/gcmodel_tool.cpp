// SPDX-License-Identifier: MPL-2.0
// The GameCube player model converter (runtime/src/gcmodel) from the command line, for checks on a computer:
//   gcmodel_tool convert Link.arc permanent_3d.pack Link.szs   the converted Link.szs (the report on stdout)
//   gcmodel_tool synth permanent_3d.pack Link.arc               a synthetic test pack for headless checks: HD Link's
//       own skeleton and material names (read from the local game, nothing of it stored), a box per shape on its
//       bone, a flat colour per texture. Written where the caller says (a run folder); never part of the repository.
// After GreenNaugahyde/ZeldaWWHDRecompAndroid's tools/gcmodels/host/host_main.cpp (MPL-2.0).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "gcmodel/gcm_io.h"
#include "gcmodel/gcmodel.h"
#include "gcmodel_synth.h"

static bool load(const char* p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

static bool save(const char* p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary);
    return f && f.write((const char*)d.data(), (std::streamsize)d.size());
}

static int usage() {
    fprintf(stderr, "usage: gcmodel_tool convert Link.arc permanent_3d.pack Link.szs\n"
                    "       gcmodel_tool synth permanent_3d.pack Link.arc\n");
    return 2;
}

static int synth(const char* packPath, const char* outPath) {
    std::vector<uint8_t> pack;
    if (!load(packPath, pack)) {
        fprintf(stderr, "ERROR: cannot read %s\n", packPath);
        return 1;
    }
    try {
        const auto szs = gcmodel::link_szs(pack);
        if (szs.empty()) throw gcmodel::Error("no Link.szs in the pack");
        const auto files = gcmodel::sarc_read(gcmodel::yaz0_decode(szs));
        const auto parts = gcmodel::describe(files.at("Link.bfres"));
        const gcmodel::PartInfo* cl = nullptr;
        for (auto& p : parts)
            if (p.name == "cl") cl = &p;
        if (!cl) throw gcmodel::Error("Link.bfres without cl");
        gcmsynth::J3DModel m;
        for (auto& b : cl->bones) {
            gcmsynth::J3DJoint j;
            j.name = b.name, j.parent = b.parent;
            for (int q = 0; q < 3; q++) j.t[q] = b.world[q] - (b.parent >= 0 ? cl->bones[b.parent].world[q] : 0);
            m.joints.push_back(j);
            m.draw.push_back({{(int)m.joints.size() - 1, 1.0}});
            m.weighted.push_back(false);
        }
        std::map<std::string, int> texIndex;
        for (size_t i = 0; i < cl->materials.size(); i++) {
            m.materials.push_back(gcmodel::gc_material(cl->materials[i]));
            int t = -1;
            if (!cl->materialTextures[i].empty()) {
                const std::string& name = cl->materialTextures[i][0];
                auto it = texIndex.find(name);
                if (it == texIndex.end()) {
                    gcmsynth::J3DTexture tex;
                    tex.name = name;
                    tex.rgba.resize(8 * 8 * 4);
                    static const uint8_t colours[][3] = {{255, 0, 255}, {0, 255, 255}, {255, 255, 0}, {0, 255, 0}, {255, 128, 0}};
                    const uint8_t* c = colours[texIndex.size() % 5];
                    for (int k = 0; k < 64; k++) memcpy(&tex.rgba[k * 4], c, 3), tex.rgba[k * 4 + 3] = 255;
                    it = texIndex.emplace(name, (int)m.textures.size()).first;
                    m.textures.push_back(tex);
                }
                t = it->second;
            }
            m.materialTexture.push_back(t);
        }
        m.uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const double h = 3.0;
        for (auto& sh : cl->shapes) {
            gcmsynth::J3DShape s;
            s.material = sh.material;
            const int base = (int)m.pos.size();
            for (int k = 0; k < 8; k++) m.pos.push_back({k & 1 ? h : -h, k & 2 ? h : -h, k & 4 ? h : -h});
            static const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
            for (auto& f : faces) {
                auto v = [&](int k, int uv) { return gcmsynth::J3DVertex{base + f[k], uv, sh.bone}; };
                s.tris.push_back({v(0, 0), v(2, 2), v(1, 1)});
                s.tris.push_back({v(0, 0), v(3, 3), v(2, 2)});
            }
            m.shapes.push_back(s);
        }
        if (!save(outPath, gcmsynth::rarc({{"cl.bdl", gcmsynth::j3d(m)}}))) throw gcmodel::Error("cannot write the output");
        printf("synthetic Link.arc: %zu joints, %zu shapes, %zu materials, %zu textures\n", m.joints.size(), m.shapes.size(),
               m.materials.size(), m.textures.size());
    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && !strcmp(argv[1], "synth")) return synth(argv[2], argv[3]);
    if (argc != 5 || strcmp(argv[1], "convert")) return usage();
    std::vector<uint8_t> arc, pack, out;
    if (!load(argv[2], arc) || !load(argv[3], pack)) {
        fprintf(stderr, "ERROR: cannot read %s or %s\n", argv[2], argv[3]);
        return 1;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = gcmodel::convert(arc, pack, out, [](int pc, const std::string& s) { fprintf(stderr, "\r%3d%% %-40s", pc, s.c_str()); });
    fprintf(stderr, "\n");
    printf("%s", r.report.c_str());
    if (!r.error.empty()) {
        fprintf(stderr, "ERROR: %s\n", r.error.c_str());
        return 1;
    }
    if (!save(argv[4], out)) {
        fprintf(stderr, "ERROR: cannot write %s\n", argv[4]);
        return 1;
    }
    printf("written %zu bytes in %.1f s\n", out.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return 0;
}
