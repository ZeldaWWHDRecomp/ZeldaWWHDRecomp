#pragma once
namespace gfxvk::bc {
inline constexpr const char* shader = R"glsl(#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) readonly buffer Input { uint src[]; };
layout(set=0,binding=1,std430) writeonly buffer Output { uint dst[]; };
layout(push_constant) uniform Params { uint width; uint height; uint slices; uint mode; } p;
uint byte_at(uint n) { return (src[n/4] >> ((n%4)*8)) & 255u; }
uint short_at(uint n) { return byte_at(n) | byte_at(n+1)*256u; }
int endpoint(uint n, bool sign) { int v=int(byte_at(n)); return sign ? max(-127,v>=128?v-256:v) : v; }
int alpha(uint base,uint pixel,bool sign) {
    int a=endpoint(base,sign),b=endpoint(base+1,sign);
    uint bit=pixel*3, offset=base+2+bit/8;
    uint index=((byte_at(offset) | (bit%8>5?byte_at(offset+1)<<8:0u)) >> (bit%8))&7u;
    int i=int(index);
    if(i==0)return a; if(i==1)return b;
    if(a>b)return ((8-i)*a+(i-1)*b)/7;
    if(i<6)return ((6-i)*a+(i-1)*b)/5;
    return i==6?(sign?-127:0):(sign?127:255);
}
uvec3 rgb(uint c) {
    uvec3 v=uvec3(c>>11,(c>>5)&63u,c&31u);
    return uvec3((v.r<<3)|(v.r>>2),(v.g<<2)|(v.g>>4),(v.b<<3)|(v.b>>2));
}
void main() {
    uint n=gl_GlobalInvocationID.y*gl_NumWorkGroups.x*64u+gl_GlobalInvocationID.x;
    if(n>=p.width*p.height*p.slices)return;
    uint x=n%p.width,y=(n/p.width)%p.height,z=n/(p.width*p.height);
    uint type=p.mode&255u;bool sign=(p.mode&256u)!=0;
    uint block=((z*((p.height+3)/4)+y/4)*((p.width+3)/4)+x/4)*(type==1||type==4?8u:16u);
    uint pixel=(y%4)*4+x%4;
    uvec4 c=uvec4(0,0,0,sign?127:255);
    if(type>=4) {
        c.r=uint(alpha(block,pixel,sign))&255u;
        if(type==5)c.g=uint(alpha(block+8,pixel,sign))&255u;
    } else {
        uint base=block+(type==1?0u:8u);
        uint c0=short_at(base),c1=short_at(base+2);
        uvec3 a=rgb(c0),b=rgb(c1);
        uint i=(byte_at(base+4+pixel/4)>>((pixel%4)*2))&3u;
        if(i==0)c.rgb=a;else if(i==1)c.rgb=b;
        else if(c0>c1||type!=1)c.rgb=i==2?(2*a+b)/3:(a+2*b)/3;
        else if(i==2)c.rgb=(a+b)/2;else c=uvec4(0);
        if(type==2)c.a=((byte_at(block+pixel/2)>>((pixel%2)*4))&15u)*17u;
        if(type==3)c.a=uint(alpha(block,pixel,false));
    }
    dst[n]=c.r|(c.g<<8)|(c.b<<16)|(c.a<<24);
}
)glsl";
}
