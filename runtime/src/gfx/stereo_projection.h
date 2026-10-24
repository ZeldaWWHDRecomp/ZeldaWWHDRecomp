#pragma once
#include <cmath>
#include <cstring>
#include <cstddef>
namespace stereo {
// The game's sun-shadow pass reconstructs negative-Z view-space positions.
// Its VS remapped[2].z is subtracted from all three reconstruction rays.
// Its PS remapped[6..17] contains three view-to-shadow matrices. Reconstruct
// relative to the displaced eye, then translate back into the centre camera's
// space before the original light matrices. This also keeps gather normals
// consistent with the off-axis projection used to draw the scene depth.
inline bool shadow_uniforms(unsigned char* bytes, size_t size, bool vertex,
                            float eye, float convergence) {
    if (size < (vertex ? 48u : 288u)) return false;
    if (vertex) {
        float value;std::memcpy(&value,bytes+40,4);
        value-=eye/convergence;std::memcpy(bytes+40,&value,4);
    } else {
        for (size_t row=6;row<18;++row) {
            float value[4];std::memcpy(value,bytes+row*16,16);
            value[3]+=value[0]*eye;std::memcpy(bytes+row*16,value,16);
        }
    }
    return true;
}
// GX2's row-major perspective matrix. Validate all four rows so orthographic
// layouts, world transforms, and light-space texture matrices are not shifted.
inline bool camera_projection(const float* p) {
    // Scanned at every 16-byte offset of every vertex uniform block, per draw and eye: the w row
    // rejects almost all of them before the remaining tests.
    if (p[14]!=-1 || p[15]!=0 || p[12]!=0 || p[13]!=0) return false;
    if (!(p[0]>0 && p[5]>0 && p[1]==0 && p[2]==0 && p[3]==0 &&
          p[4]==0 && p[6]==0 && p[7]==0 && p[8]==0 && p[9]==0 && p[10]<=-1 && p[11]<0)) return false;
    for (int i=0;i<16;++i) if (!std::isfinite(p[i])) return false;
    return true;
}
inline bool project_eye(float* p, float eye, float convergence) {
    if (!camera_projection(p)) return false;
    const float projectedEye = eye*p[0];
    p[2] -= projectedEye/convergence;
    p[3] -= projectedEye;
    return true;
}
// The x rows project_eye shifted in a packed buffer, with their unshifted values: the other eye's
// bytes are the same buffer with these rows shifted for its eye (apply_eye_rows), exactly as
// project_eye computes them.
struct EyeRows {
    struct Row { unsigned offset; float p0, p2, p3; };
    Row rows[16];
    int count = 0;
    bool overflow = false;
    void add(size_t offset, const float* unshifted) {
        if (count < 16) rows[count++] = {unsigned(offset), unshifted[0], unshifted[2], unshifted[3]};
        else overflow = true;
    }
};
inline void apply_eye_rows(unsigned char* bytes, const EyeRows& rows, float eye, float convergence) {
    for (int i=0;i<rows.count;++i) {
        const auto& row = rows.rows[i];
        const float projectedEye = eye*row.p0;
        const float shifted[2] = {row.p2 - projectedEye/convergence, row.p3 - projectedEye};
        std::memcpy(bytes+row.offset+8, shifted, sizeof shifted);
    }
}
// Whether project_uniform_block would change the block (read in place: no copy without a camera).
inline bool has_camera_projection(const unsigned char* bytes, size_t size) {
    for (size_t at=0;at+64<=size;at+=16) {
        float p[16];std::memcpy(p,bytes+at,64);
        if (camera_projection(p)) return true;
    }
    return false;
}
inline void project_uniform_block(unsigned char* bytes, size_t size, float eye, float convergence,
                                  const void* excludedProjection = nullptr,
                                  EyeRows* rows = nullptr, size_t rowBase = 0) {
    for (size_t at=0;at+64<=size;at+=16) {
        if (excludedProjection && !std::memcmp(bytes+at,excludedProjection,64)) continue;
        float p[16];std::memcpy(p,bytes+at,64);
        const float unshifted[4] = {p[0],p[1],p[2],p[3]};
        if (project_eye(p,eye,convergence)) {
            if (rows) rows->add(rowBase+at,unshifted);
            std::memcpy(bytes+at,p,16);
            at+=48;
        }
    }
}
}
