// FXAA (the classic console variant) in GLSL, shared by the window composition (present.cpp, FXAA when the
// picture is shown) and the TV picture's smoothing at its HUD point (reproj.cpp smooth_scene, #587), so both
// smooth alike. Needs `uniform sampler2D image` (linear filtering) declared before it; evaluated on the
// image's texel grid at `at`.
#pragma once
#define WWHD_FXAA_GLSL R"glsl(float luma(vec3 c) { return dot(c,vec3(0.299,0.587,0.114)); }
vec3 fxaa(vec2 at) {
 vec2 rcp=1.0/vec2(textureSize(image,0));
 vec3 nw=texture(image,at+vec2(-1,-1)*rcp).rgb, ne=texture(image,at+vec2(1,-1)*rcp).rgb;
 vec3 sw=texture(image,at+vec2(-1,1)*rcp).rgb, se=texture(image,at+vec2(1,1)*rcp).rgb;
 vec3 m=texture(image,at).rgb;
 float lnw=luma(nw),lne=luma(ne),lsw=luma(sw),lse=luma(se),lm=luma(m);
 float lmin=min(lm,min(min(lnw,lne),min(lsw,lse))),lmax=max(lm,max(max(lnw,lne),max(lsw,lse)));
 if(lmax-lmin<max(0.0312,lmax*0.125))return m;
 vec2 dir=vec2(-((lnw+lne)-(lsw+lse)),(lnw+lsw)-(lne+lse));
 float reduce=max((lnw+lne+lsw+lse)*(0.25/8.0),1.0/128.0);
 dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),vec2(-8.0),vec2(8.0))*rcp;
 vec3 a=0.5*(texture(image,at+dir*(1.0/3.0-0.5)).rgb+texture(image,at+dir*(2.0/3.0-0.5)).rgb);
 vec3 b=a*0.5+0.25*(texture(image,at-dir*0.5).rgb+texture(image,at+dir*0.5).rgb);
 float lb=luma(b);return (lb<lmin||lb>lmax)?a:b;
}
)glsl"
