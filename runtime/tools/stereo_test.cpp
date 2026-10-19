#include "gfx/stereo.h"
#include "gfx/stereo_projection.h"
#include <cassert>
#include <limits>
int main() {
    float original[16]={2,0,0,0, 0,3,0,0, 0,0,-1.01f,-20, 0,0,-1,0};
    for (float eye : {-8.0f,0.0f,8.0f}) {
        float p[16];std::memcpy(p,original,sizeof p);
        assert(stereo::project_eye(p,eye,600));
        for (float z : {100.0f,600.0f,1200.0f}) {
            const float clipX = p[0]*20+p[2]*-z+p[3];
            const float expected = original[0]*20+stereo::clip_shift(z,eye*original[0],600);
            assert(std::fabs(clipX-expected)<0.0001f);
            // Projected screen coordinates must come from this same eye matrix.
            const float projectedU=(clipX+z)*0.5f;
            assert(std::fabs(projectedU/z-(clipX/z*0.5f+0.5f))<0.00001f);
        }
    }
    float ortho[16]={1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,0,1};
    assert(!stereo::project_eye(ortho,8,600));
    // Unsupported shadow blocks must be left byte-for-byte intact.
    for (bool vertex : {false,true}) {
        const size_t minimum=vertex?48:288;
        std::array<unsigned char,288> bytes;bytes.fill(0x5a);
        const auto originalBytes=bytes;
        for(size_t size : {size_t(0),minimum-1}) {
            assert(!stereo::shadow_uniforms(bytes.data(),size,vertex,8,600));
            assert(bytes==originalBytes);
        }
    }
    for (float eye : {-3.2f,3.2f}) for (float depth : {50.0f,600.0f,2000.0f}) {
        float camera[16];std::memcpy(camera,original,sizeof camera);
        stereo::project_eye(camera,eye,600);
        const float worldX=17;
        const float ndc=(camera[0]*worldX-camera[2]*depth+camera[3])/depth;
        float rays[12]={};
        assert(stereo::shadow_uniforms(reinterpret_cast<unsigned char*>(rays),sizeof rays,true,eye,600));
        const float reconstructed=(-ndc/original[0]-rays[10])*-depth;
        float shadows[72]={};
        for (int row=6;row<18;++row) { shadows[row*4]=.25f;shadows[row*4+3]=2; }
        assert(stereo::shadow_uniforms(reinterpret_cast<unsigned char*>(shadows),sizeof shadows,false,eye,600));
        for (int row=6;row<18;++row)
            assert(std::fabs(shadows[row*4]*reconstructed+shadows[row*4+3]-(.25f*worldX+2))<.0001f);
    }
    for (float a : {1.0f,1.6f,16.0f/9,21.0f/9,32.0f/9,4.0f}) {
        float w=720*a,h=720;
        for (int mode : {stereo::HalfSBS,stereo::TAB}) {
            auto l=stereo::pack({0,0,w,h},w,h,mode,0);
            auto r=stereo::pack({0,0,w,h},w,h,mode,1);
            if(mode==stereo::TAB) assert(l.y+l.h==r.y && r.y+r.h==h);
            else assert(l.x+l.w==r.x && r.x+r.w==w);
        }
    }
    assert(stereo::clip_shift(600,8,600)==0);
    assert(stereo::clip_shift(300,8,600)<0);
    assert(stereo::clip_shift(1200,8,600)>0);
    for(float z : {10.0f,300.0f,600.0f,1200.0f}) {
        assert(stereo::clip_shift(z,0,600)==0);
        assert(stereo::clip_shift(z,8,600)==-stereo::clip_shift(z,-8,600));
    }
    stereo::set_mode(-1); assert(stereo::requestedMode==stereo::Off);
    stereo::set_depth(std::numeric_limits<float>::quiet_NaN()); assert(stereo::requestedDepth==1);
    stereo::set_convergence(0); assert(stereo::requestedConvergence==10);
    stereo::set_mode(stereo::TAB);stereo::latch();
    stereo::set_mode(stereo::Off);assert(stereo::frame.mode==stereo::TAB);
    // HD layouts (HUD, menus, text): every projection with a tagged layout's y and z rows keeps
    // zero disparity, also with a differently narrowed x row; game cameras are still shifted.
    float layout[16]={1.4f,0,0,0, 0,2.74748f,0,0, 0,0,-1.0002f,-2.0002f, 0,0,-1,0};
    uint32_t words[16];std::memcpy(words,layout,sizeof words);
    stereo::tag_layout(0x40,words);
    layout[0]=1.54546f;
    float hud[16];std::memcpy(hud,layout,sizeof hud);
    assert(!stereo::project_eye(hud,8,600)&&!std::memcmp(hud,layout,sizeof hud));
    float camera[16];std::memcpy(camera,original,sizeof camera);
    assert(stereo::project_eye(camera,8,600));
    for (int i=0;i<12;++i) { float other[16];std::memcpy(other,original,sizeof other);other[11]=-30.0f-i;stereo::add_layout_signature(other); }
    assert(stereo::layoutSignatureCount==8);
    // The game's own layout projection keeps zero disparity even if it was never tagged.
    stereo::layoutSignatureCount=0;
    const float n=1,f=10000,t=std::tan(20.0f*3.14159265f/180.0f);
    float untagged[16]={1/(t*16/9.0f),0,0,0, 0,1/t,0,0, 0,0,-(f+n)/(f-n),-2*f*n/(f-n), 0,0,-1,0};
    float moved[16];std::memcpy(moved,untagged,sizeof moved);
    assert(!stereo::project_eye(moved,8,600)&&!std::memcmp(moved,untagged,sizeof moved));
}
