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
    stereo::set_hud_distance(0); assert(stereo::requestedHudDistance==10);
    stereo::set_hud_distance(150);stereo::set_convergence(600);
    stereo::set_mode(stereo::TAB);stereo::latch();
    // the 2D layouts sit where a scene point at the HUD distance does: in front of the screen plane
    // (crossed: the left eye's picture moves right), as clip_shift places a point that near
    stereo::mainCamera[0]=1;
    assert(stereo::layout_shift(stereo::eye_offset(0))>0 && stereo::layout_shift(stereo::eye_offset(1))<0);
    assert(std::fabs(stereo::layout_shift(8)-stereo::clip_shift(150,8,stereo::frame.convergence)/150)<1e-6f);
    // behind the glass: the HUD distance is the screen plane
    stereo::requestedBehindGlass=true;stereo::latch();
    assert(stereo::frame.convergence==150 && stereo::layout_shift(8)==0);
    stereo::requestedBehindGlass=false;stereo::latch();
    // the automatic HUD distance: the game's linearization (near 3, far 375000), a margin in front of the nearest
    // sample, nearer at once, farther slowly, never nearer than kNearestHud
    const float k=1-3.0f/375000;
    assert(std::fabs(stereo::hud::distance((1-3.0f/100)/k,3,k)-100)<0.5f);
    assert(stereo::hud::distance(1,3,k)==0 && stereo::hud::distance(0.1f,3,k)==0);
    float depths[2]={(1-3.0f/100)/k,(1-3.0f/1000)/k};
    stereo::hud::observe(depths,2,3,k);assert(std::fabs(stereo::hud::nearest-100)<0.5f);
    stereo::hud::followed=150;
    const float first=stereo::hud::follow(150,false);assert(first<120 && first>80);
    for(int i=0;i<20;++i)stereo::hud::follow(150,false);
    assert(std::fabs(stereo::hud::followed-80)<1);
    float far[1]={(1-3.0f/1000)/k};stereo::hud::observe(far,1,3,k);
    assert(stereo::hud::follow(150,false)<85);  // (receding slowly)
    float wall[1]={(1-3.0f/15)/k};stereo::hud::observe(wall,1,3,k);
    for(int i=0;i<30;++i)stereo::hud::follow(150,false);
    assert(std::fabs(stereo::hud::followed-stereo::hud::kNearestHud)<0.5f);
    stereo::hud::nearest=-1;stereo::hud::followed=150;
    stereo::set_mode(stereo::Off);assert(stereo::frame.mode==stereo::TAB);
    // Lens values do not identify a layout. A 3D camera with the same lens must
    // retain disparity, even after those values have been uploaded by nw::lyt.
    float layout[16]={1.4f,0,0,0, 0,2.74748f,0,0, 0,0,-1.0002f,-2.0002f, 0,0,-1,0};
    uint32_t words[16];std::memcpy(words,layout,sizeof words);
    stereo::tag_layout(0x40,words);
    layout[0]=1.54546f;
    float hud[16];std::memcpy(hud,layout,sizeof hud);
    assert(stereo::project_eye(hud,8,600)&&hud[3]!=0);
    float camera[16];std::memcpy(camera,original,sizeof camera);
    assert(stereo::project_eye(camera,8,600));
    const float n=1,f=10000,t=std::tan(20.0f*3.14159265f/180.0f);
    float untagged[16]={1/(t*16/9.0f),0,0,0, 0,1/t,0,0, 0,0,-(f+n)/(f-n),-2*f*n/(f-n), 0,0,-1,0};
    float moved[16];std::memcpy(moved,untagged,sizeof moved);
    assert(stereo::project_eye(moved,8,600)&&moved[3]!=0);
    stereo::layout_draw(true);stereo::layout_draw(true);
    assert(stereo::layoutDrawDepth==2);
    stereo::layout_draw(false);assert(stereo::layoutDrawDepth==1);
    stereo::layout_draw(false);stereo::layout_draw(false);
    assert(stereo::layoutDrawDepth==0);
}
