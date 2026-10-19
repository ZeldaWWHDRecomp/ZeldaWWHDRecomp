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
    for (int i=0;i<16;++i) if (!std::isfinite(p[i])) return false;
    return p[0]>0 && p[5]>0 && p[1]==0 && p[2]==0 && p[3]==0 &&
        p[4]==0 && p[6]==0 && p[7]==0 && p[8]==0 && p[9]==0 &&
        p[10]<=-1 && p[11]<0 && p[12]==0 && p[13]==0 && p[14]==-1 && p[15]==0;
}
// The HD layouts (nw::lyt, aspect.cpp) draw the HUD, menus and message text through a
// perspective projection of their own (fovy 40, near 1, far 10000). Every draw through one keeps
// zero disparity, so all 2D information shares one plane, whichever way the matrix reaches the
// shader (registers or a uniform block) and whichever layout was tagged last (TV or GamePad).
// The TV's upload narrows the x row for the aspect ratio: the y and z rows identify it.
struct LayoutSignature { float y, z, w; };
inline LayoutSignature layoutSignatures[8];
inline int layoutSignatureCount = 0, layoutSignatureNext = 0;  // render thread, like the tag in stereo.h
inline bool layout_projection(const float* p) {
    // The game's own value (DrawInfo's fovy 40, near 1, far 10000): also for layouts whose
    // projection reaches the shader without the tag (the hearts, the B button's frame).
    if (std::fabs(p[5]-2.747477f)<1e-4f && std::fabs(p[10]+1.0002f)<2e-6f &&
        std::fabs(p[11]+2.0002f)<1e-4f) return true;
    for (int i=0;i<layoutSignatureCount;++i)
        if (p[5]==layoutSignatures[i].y && p[10]==layoutSignatures[i].z && p[11]==layoutSignatures[i].w)
            return true;
    return false;
}
inline void add_layout_signature(const float* p) {
    if (!camera_projection(p) || layout_projection(p)) return;
    layoutSignatures[layoutSignatureNext]={p[5],p[10],p[11]};
    layoutSignatureNext=(layoutSignatureNext+1)%8;
    if (layoutSignatureCount<8) ++layoutSignatureCount;
}
inline bool project_eye(float* p, float eye, float convergence) {
    if (!camera_projection(p) || layout_projection(p)) return false;
    const float projectedEye = eye*p[0];
    p[2] -= projectedEye/convergence;
    p[3] -= projectedEye;
    return true;
}
inline void project_uniform_block(unsigned char* bytes, size_t size, float eye, float convergence,
                                  const void* excludedProjection = nullptr) {
    for (size_t at=0;at+64<=size;at+=16) {
        if (excludedProjection && !std::memcmp(bytes+at,excludedProjection,64)) continue;
        float p[16];std::memcpy(p,bytes+at,64);
        if (project_eye(p,eye,convergence)) {
            std::memcpy(bytes+at,p,16);
            at+=48;
        }
    }
}
}
