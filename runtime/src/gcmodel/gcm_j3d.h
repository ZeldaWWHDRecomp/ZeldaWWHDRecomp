#pragma once
/* SPDX-License-Identifier: MPL-2.0
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/gcmodel/, taken over
 * as it is; only the glue to this port changed. */
// J3D models (GameCube .bdl/.bmd) for gcmodel.h: joints, envelopes, draw matrices, vertex arrays, shapes as
// triangles, materials' textures (port of the fork's tools/gcmodels/j3d.py).
#include <array>
#include <tuple>
#include <map>
#include <string>
#include <vector>

#include "gcm_io.h"

namespace gcmodel {

using Mat = std::array<std::array<double, 4>, 3>;  // 3x4, rows
using Vec3 = std::array<double, 3>;

Mat mat_identity();
Mat mat_mul(const Mat& a, const Mat& b);
Vec3 mat_apply(const Mat& m, const Vec3& v);
Vec3 rot_apply(const Mat& m, const Vec3& v);
Mat mat_inv(const Mat& m);  // (a joint scaled to nothing: a near pseudo-inverse)
// scale, then rotations X, Y, Z (radians), then translation
Mat mat_srt(const Vec3& s, const Vec3& r, const Vec3& t);

enum : int { PNMTXIDX = 0, POS = 9, NRM = 10, CLR0 = 11, CLR1 = 12, TEX0 = 13 };

struct Vertex {
    int pos = -1, nrm = -1, clr = -1, tex = -1;
    int mtx = 0;  // draw matrix (DRW1) index
    bool operator<(const Vertex& o) const {
        return std::tie(pos, nrm, tex, clr, mtx) < std::tie(o.pos, o.nrm, o.tex, o.clr, o.mtx);
    }
};
using Tri = std::array<Vertex, 3>;

struct Joint {
    std::string name;
    int parent = -1;
    Mat local{}, world{};
};

struct Shape {
    int material = -1;
    std::vector<Tri> tris;
};

struct Texture {  // decoded, rows of RGBA
    int format = 0, width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

struct Model {
    std::map<std::string, Bytes> chunks;
    std::vector<Joint> joints;
    std::vector<std::vector<std::pair<int, double>>> envelopes;
    std::map<int, Mat> invBind;
    std::vector<std::vector<std::pair<int, double>>> draw;  // per draw matrix: (joint, weight)
    std::vector<bool> drawWeighted;
    std::vector<std::array<double, 3>> pos, nrm;
    std::vector<std::array<double, 2>> uv;
    std::vector<std::array<uint8_t, 4>> clr;
    std::vector<Shape> shapes;
    std::vector<std::string> materialNames, textureNames;
    std::vector<std::vector<int>> materialTextures;  // TEX1 indices
    std::map<std::string, Texture> textures;          // by name (the first of a name)

    explicit Model(const Bytes& bdl);
    Vec3 position(const Vertex& v) const;  // in the bind pose (model space)
    Vec3 normal(const Vertex& v) const;    // into model space
    bool hasChunk(const char* c) const { return chunks.count(c) != 0; }
};

// TEX1-style headers (0x20 bytes at `h` in `base`, data relative to the header)
Texture decode_bti(const uint8_t* base, size_t size, size_t h);
Texture decode_bti_file(const Bytes& bti);  // a .bti of its own (linktexbci4.bti)

}  // namespace gcmodel
