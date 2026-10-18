#include "story_world_placement.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
namespace story_world_placement { namespace {
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Point transformed(Point p,Point t,Point r,Point s){
 constexpr double radians=3.14159265358979323846/180;
 double x=p.x*s.x,y=p.y*s.y,z=p.z*s.z;
 double c=std::cos(r.z*radians),sn=std::sin(r.z*radians),a=x*c-y*sn;y=x*sn+y*c;x=a;
 c=std::cos(r.x*radians);sn=std::sin(r.x*radians);a=y*c-z*sn;z=y*sn+z*c;y=a;
 c=std::cos(r.y*radians);sn=std::sin(r.y*radians);a=x*c+z*sn;z=-x*sn+z*c;x=a;
 return {float(x+t.x),float(y+t.y),float(z+t.z)};
}
void include(Bounds&b,Point p){b.minimum={std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};b.maximum={std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};}
Bounds empty(){const float i=std::numeric_limits<float>::infinity();return {{i,i,i},{-i,-i,-i}};}
bool overlap(const Bounds&a,const Bounds&b,float pad){return a.minimum.x<=double(b.maximum.x)+pad&&a.maximum.x>=double(b.minimum.x)-pad&&a.minimum.y<=double(b.maximum.y)+pad&&a.maximum.y>=double(b.minimum.y)-pad&&a.minimum.z<=double(b.maximum.z)+pad&&a.maximum.z>=double(b.minimum.z)-pad;}
bool segment_box(Point a,Point b,const Bounds&box,float pad){
 double lo=0,hi=1;const float av[]={a.x,a.y,a.z},bv[]={b.x,b.y,b.z},mn[]={box.minimum.x,box.minimum.y,box.minimum.z},mx[]={box.maximum.x,box.maximum.y,box.maximum.z};
 for(unsigned k=0;k<3;++k){double d=double(bv[k])-av[k],l=double(mn[k])-pad,h=double(mx[k])+pad;if(d==0){if(av[k]<l||av[k]>h)return false;continue;}double p=(l-av[k])/d,q=(h-av[k])/d;if(p>q)std::swap(p,q);lo=std::max(lo,p);hi=std::min(hi,q);if(lo>hi)return false;}return true;
}
using Bytes=std::vector<uint8_t>;
bool range(const Bytes&b,uint64_t p,uint64_t n){return p<=b.size()&&n<=b.size()-p;}
uint16_t u16(const Bytes&b,size_t p){return uint16_t(b[p])<<8|b[p+1];}
uint32_t u32(const Bytes&b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
float f32(const Bytes&b,size_t p){return std::bit_cast<float>(u32(b,p));}
Point point(const Bytes&b,size_t p){return {f32(b,p),f32(b,p+4),f32(b,p+8)};}
bool fail(std::string*w,const char*m){if(w)*w=m;return false;}
Coverage& coverage(Index&i,const std::string&s,int r){for(auto&c:i.coverage)if(c.stage==s&&c.room==r)return c;i.coverage.push_back({s,r,false,false,false,{}});return i.coverage.back();}
struct Chunk {std::string tag;uint32_t count,offset;};
bool chunks(const Bytes&b,std::vector<Chunk>&out){if(!range(b,0,4))return false;uint32_t n=u32(b,0);if(!n||n>256||!range(b,4,uint64_t(n)*12))return false;for(unsigned j=0;j<n;++j){size_t p=4+j*12;std::string tag(reinterpret_cast<const char*>(b.data()+p),4);for(unsigned char c:tag)if(!std::isalnum(c))return false;uint32_t at=u32(b,p+8);if(at<4+n*12||at>b.size())return false;out.push_back({tag,u32(b,p+4),at});}return true;}
}
bool valid(const Bounds&b){return finite(b.minimum)&&finite(b.maximum)&&b.minimum.x<=b.maximum.x&&b.minimum.y<=b.maximum.y&&b.minimum.z<=b.maximum.z;}
bool transform_bounds(const Bounds&b,Point t,Point r,Point s,Bounds&out){if(!valid(b)||!finite(t)||!finite(r)||!finite(s)||s.x==0||s.y==0||s.z==0)return false;Bounds result=empty();for(unsigned k=0;k<8;++k)include(result,transformed({k&1?b.maximum.x:b.minimum.x,k&2?b.maximum.y:b.minimum.y,k&4?b.maximum.z:b.minimum.z},t,r,s));if(!valid(result))return false;out=result;return true;}
std::vector<Conflict> check(const std::string&s,int room,const Bounds&b,const Index&i,float pad){
 std::vector<Conflict> out;if(s.empty()||room<0||room>63||!valid(b)||!std::isfinite(pad)||pad<0){out.push_back({"invalid","","Invalid world bounds or clearance"});return out;}
 const Coverage*c=nullptr;for(const auto&v:i.coverage)if(v.stage==s&&v.room==room){c=&v;break;}
 if(!c||!c->collision||!c->routes||!c->events)out.push_back({"coverage","",c&&!c->reason.empty()?c->reason:"Native collision, route or event coverage is unavailable for this room"});
 for(const auto&v:i.regions)if(v.stage==s&&v.room==room){if(!valid(v.bounds)||b.minimum.x<v.bounds.minimum.x||b.minimum.y<v.bounds.minimum.y||b.minimum.z<v.bounds.minimum.z||b.maximum.x>v.bounds.maximum.x||b.maximum.y>v.bounds.maximum.y||b.maximum.z>v.bounds.maximum.z)out.push_back({"coverage","room-boundary","Placement extends beyond the indexed room; neighbouring room coverage is required"});}
 for(const auto&v:i.bounds)if(v.stage==s&&(v.room<0||v.room==room)){if(!valid(v.bounds)){out.push_back({"coverage",v.id,"Protected bounds are invalid"});continue;}// Resting on a native floor is permitted; penetrating triangles and walls remain protected.
  if(v.kind=="collision"&&v.bounds.maximum.y<=b.minimum.y+.01f)continue;
  if(overlap(b,v.bounds,pad))out.push_back({v.kind,v.id,"Placement intersects protected "+v.kind+" bounds"});}
 for(const auto&v:i.routes)if(v.stage==s&&(v.room<0||v.room==room)){
  bool invalid=!std::isfinite(v.radius)||v.radius<0||v.points.empty();for(auto p:v.points)invalid|=!finite(p);
  if(invalid){out.push_back({"coverage",v.id,"Protected route data is invalid"});continue;}
  Bounds horizontal=b;horizontal.minimum.y=horizontal.maximum.y=0;auto flat=[](Point p){p.y=0;return p;};
  bool hit=false;for(size_t j=0;j<v.points.size();++j){if(j+1<v.points.size())hit|=segment_box(flat(v.points[j]),flat(v.points[j+1]),horizontal,pad+v.radius);else if(v.closed||v.points.size()==1)hit|=segment_box(flat(v.points[j]),flat(v.points[0]),horizontal,pad+v.radius);}
  if(hit)out.push_back({"route",v.id,"Placement intersects a protected route corridor"});
 }return out;
}
namespace {
struct SweepHit {double time=2;bool x=false,z=false;Conflict conflict;};
Bounds shifted(Bounds b,Point p){b.minimum.x+=p.x;b.maximum.x+=p.x;b.minimum.z+=p.z;b.maximum.z+=p.z;return b;}
// Continuous separating-axis intervals. For a route, use the segment normal as
// well as X/Z, so a diagonal corridor does not become its entire bounding box.
SweepHit projected_hit(const Bounds& b,Point delta,Point a,Point end,double radius,bool route,Conflict conflict,double extra_x=0,double extra_z=0){
 const double cx=(double(b.minimum.x)+b.maximum.x)/2,cz=(double(b.minimum.z)+b.maximum.z)/2;
 const double hx=(double(b.maximum.x)-b.minimum.x)/2+extra_x,hz=(double(b.maximum.z)-b.minimum.z)/2+extra_z;
 double entry=0,exit=1;bool hit_x=false,hit_z=false;
 auto axis=[&](double nx,double nz){
  const double center=cx*nx+cz*nz,velocity=delta.x*nx+delta.z*nz;
  const double extent=hx*std::fabs(nx)+hz*std::fabs(nz)+radius*(std::fabs(nx)+std::fabs(nz));
  const double p=a.x*nx+a.z*nz,q=end.x*nx+end.z*nz;
  const double low=std::min(p,q)-extent,high=std::max(p,q)+extent;
  // Touching and moving away/tangentially cannot enter this obstacle.
  if((center<=low&&velocity<=0)||(center>=high&&velocity>=0))return false;
  if(velocity==0)return center>=low&&center<=high;
  double near=(low-center)/velocity,far=(high-center)/velocity;if(near>far)std::swap(near,far);
  if(near>entry+1e-12){entry=near;hit_x=nx!=0;hit_z=nz!=0;}
  else if(std::fabs(near-entry)<=1e-12){hit_x|=nx!=0;hit_z|=nz!=0;}
  exit=std::min(exit,far);return entry<=exit;
 };
 if(!axis(1,0)||!axis(0,1))return {};
 if(route){const double nx=double(end.z)-a.z,nz=double(a.x)-end.x;if((nx!=0||nz!=0)&&!axis(nx,nz))return {};}
 if(entry<0||entry>1||exit<0||(!hit_x&&!hit_z))return {};
 return {entry,hit_x,hit_z,std::move(conflict)};
}
void add_conflict(std::vector<Conflict>&out,const Conflict&c){for(const auto&old:out)if(old.kind==c.kind&&old.id==c.id)return;out.push_back(c);}
}
SweepResult sweep(const std::string&s,int room,const Bounds&start,Point desired,const Index&i,float pad){
 SweepResult out;
 if(!finite(desired)||desired.y!=0){out.blocked=true;out.conflicts.push_back({"invalid","","Swept placement requires a finite horizontal delta"});return out;}
 out.conflicts=check(s,room,start,i,pad);if(!out.conflicts.empty()){out.blocked=true;return out;}
 Point remaining=desired;
 for(unsigned pass=0;pass<3&&(remaining.x!=0||remaining.z!=0);++pass){
  const Bounds current=shifted(start,out.allowed_delta);SweepHit first;std::vector<Conflict> contacts;
  auto consider=[&](const SweepHit&hit){if(hit.time<first.time-1e-12){first=hit;contacts={hit.conflict};}else if(hit.time<=1&&std::fabs(hit.time-first.time)<=1e-12){first.x|=hit.x;first.z|=hit.z;add_conflict(contacts,hit.conflict);}};
  for(const auto&v:i.bounds)if(v.stage==s&&(v.room<0||v.room==room)){
   if(v.kind=="collision"&&v.bounds.maximum.y<=start.minimum.y+.01f)continue;
   if(double(current.minimum.y)>double(v.bounds.maximum.y)+pad||double(current.maximum.y)<double(v.bounds.minimum.y)-pad)continue;
   // Reject distant triangles before constructing diagnostics or solving intervals.
   if(double(current.minimum.x)+std::min(0.0,double(remaining.x))>double(v.bounds.maximum.x)+pad||double(current.maximum.x)+std::max(0.0,double(remaining.x))<double(v.bounds.minimum.x)-pad||double(current.minimum.z)+std::min(0.0,double(remaining.z))>double(v.bounds.maximum.z)+pad||double(current.maximum.z)+std::max(0.0,double(remaining.z))<double(v.bounds.minimum.z)-pad)continue;
   // A rectangle is a degenerate segment expanded by independent X/Z extents.
   const double cx=(double(v.bounds.minimum.x)+v.bounds.maximum.x)/2,cz=(double(v.bounds.minimum.z)+v.bounds.maximum.z)/2;
   const double hx=(double(v.bounds.maximum.x)-v.bounds.minimum.x)/2,hz=(double(v.bounds.maximum.z)-v.bounds.minimum.z)/2;
   consider(projected_hit(current,remaining,{float(cx),0,float(cz)},{float(cx),0,float(cz)},0,false,{v.kind,v.id,"Drift reached protected "+v.kind+" bounds"},hx+pad,hz+pad));
  }
  for(const auto&v:i.routes)if(v.stage==s&&(v.room<0||v.room==room))for(size_t k=0;k<v.points.size();++k){
   if(k+1==v.points.size()&&!v.closed&&v.points.size()!=1)break;
   consider(projected_hit(current,remaining,v.points[k],v.points[(k+1)%v.points.size()],double(pad)+v.radius,true,{"route",v.id,"Drift reached a protected route corridor"}));
  }
  for(const auto&v:i.regions)if(v.stage==s&&v.room==room){
   auto boundary=[&](float d,double low,double high,bool x){if(d==0)return;const double time=d>0?high/d:low/d;if(time>=0&&time<=1)consider({time,x,!x,{"coverage","room-boundary","Drift reached the indexed room boundary"}});};
   boundary(remaining.x,double(v.bounds.minimum.x)-current.minimum.x,double(v.bounds.maximum.x)-current.maximum.x,true);
   boundary(remaining.z,double(v.bounds.minimum.z)-current.minimum.z,double(v.bounds.maximum.z)-current.maximum.z,false);
  }
  if(first.time>1){out.allowed_delta.x+=remaining.x;out.allowed_delta.z+=remaining.z;remaining={};break;}
  out.blocked=true;for(const auto&contact:contacts)add_conflict(out.conflicts,contact);
  const double magnitude=std::max(std::fabs(double(remaining.x)),std::fabs(double(remaining.z)));
  const double coordinate=std::max({std::fabs(double(current.minimum.x)),std::fabs(double(current.maximum.x)),std::fabs(double(current.minimum.z)),std::fabs(double(current.maximum.z)),1.0});
  const double skin=std::max(.1,coordinate*4*std::numeric_limits<float>::epsilon());
  const double advance=std::max(0.0,first.time-skin/magnitude);
  out.allowed_delta.x+=float(remaining.x*advance);out.allowed_delta.z+=float(remaining.z*advance);
  remaining.x=first.x?0:float(remaining.x*(1-advance));remaining.z=first.z?0:float(remaining.z*(1-advance));
 }
 auto final=check(s,room,shifted(start,out.allowed_delta),i,pad);
 if(!final.empty()){out.allowed_delta={};out.blocked=true;for(const auto&c:final)add_conflict(out.conflicts,c);}
 return out;
}
bool add_native_collision(Index&i,const std::string&s,int room,const Bytes&b,Point t,float yaw,std::string*w){
 if(!range(b,0,0x34)||!finite(t)||!std::isfinite(yaw))return fail(w,"Invalid native collision header or room transform");
 const uint32_t nv=u32(b,0),vp=u32(b,4),nt=u32(b,8),tp=u32(b,12);
 if(nv>65535||nt>1000000||!range(b,vp,uint64_t(nv)*12)||!range(b,tp,uint64_t(nt)*10)||((nv==0)!=(nt==0)))return fail(w,"Invalid native collision tables");
 std::vector<Point> vertices;vertices.reserve(nv);for(uint32_t j=0;j<nv;++j){auto p=point(b,vp+j*12);if(!finite(p))return fail(w,"Non-finite native collision vertex");p=transformed(p,t,{0,yaw,0},{1,1,1});if(!finite(p))return fail(w,"Native collision transform overflow");vertices.push_back(p);}
 std::vector<ProtectedBounds> added;added.reserve(nt);for(uint32_t j=0;j<nt;++j){Bounds box=empty();for(unsigned k=0;k<3;++k){uint16_t index=u16(b,tp+j*10+k*2);if(index>=nv)return fail(w,"Invalid native triangle vertex");include(box,vertices[index]);}added.push_back({s,room,"collision","native-triangle-"+std::to_string(j),box});}
 i.bounds.insert(i.bounds.end(),added.begin(),added.end());coverage(i,s,room).collision=true;return true;
}
bool add_native_layout(Index&i,const std::string&s,int room,const Bytes&b,Point t,float yaw,std::string*w){
 std::vector<Chunk> cs;if(!chunks(b,cs)||!finite(t)||!std::isfinite(yaw))return fail(w,"Invalid native room layout");
 std::vector<ProtectedBounds> actors;std::vector<ProtectedRoute> routes;
 for(const auto&c:cs){const bool actor=c.tag=="ACTR"||c.tag=="SCOB"||c.tag=="TGOB"||c.tag=="TGSC"||c.tag=="TRES"||c.tag=="DOOR"||c.tag=="TGDR"||c.tag.rfind("ACT",0)==0||c.tag.rfind("SCO",0)==0||c.tag.rfind("TRE",0)==0;
  if(actor){const unsigned stride=(c.tag=="SCOB"||c.tag=="TGSC"||c.tag.rfind("SCO",0)==0)?36:32;if(c.count>65535||!range(b,c.offset,uint64_t(c.count)*stride))return fail(w,"Invalid native actor table");for(uint32_t j=0;j<c.count;++j){size_t at=c.offset+j*stride;Point p=point(b,at+12);if(!finite(p))return fail(w,"Invalid native event position");p=transformed(p,t,{0,yaw,0},{1,1,1});float radius=10000; // All authored actors/tags, all layers: fixed-event exclusion.

   std::string name(reinterpret_cast<const char*>(b.data()+at),strnlen(reinterpret_cast<const char*>(b.data()+at),8));actors.push_back({s,room,"event",c.tag+":"+name+":"+std::to_string(j),{{p.x-radius,-1000000,p.z-radius},{p.x+radius,1000000,p.z+radius}}});}}
  if(c.tag=="RPAT"||c.tag=="PATH"){
   if(c.count>65535||!range(b,c.offset,uint64_t(c.count)*12))return fail(w,"Invalid native path table");for(uint32_t j=0;j<c.count;++j){size_t at=c.offset+j*12;uint16_t count=u16(b,at);uint32_t point_offset=u32(b,at+8),points=0;bool in_points=false;for(const auto& pc:cs)if(((c.tag=="RPAT"&&pc.tag=="RPPN")||(c.tag=="PATH"&&pc.tag=="PPNT"))&&range(b,pc.offset,uint64_t(pc.count)*16)&&point_offset%16==0&&uint64_t(point_offset)+uint64_t(count)*16<=uint64_t(pc.count)*16){points=pc.offset+point_offset;in_points=true;}if(count>4096||!range(b,points,uint64_t(count)*16)||(count&&!in_points))return fail(w,"Invalid native path points");ProtectedRoute route{s,room,c.tag+":"+std::to_string(j),{},2000,b[at+5]!=0};for(unsigned k=0;k<count;++k){auto p=point(b,points+k*16+4);if(!finite(p))return fail(w,"Invalid native path coordinate");p=transformed(p,t,{0,yaw,0},{1,1,1});route.points.push_back(p);}if(!route.points.empty())routes.push_back(std::move(route));}
  }
 }
 i.bounds.insert(i.bounds.end(),actors.begin(),actors.end());i.routes.insert(i.routes.end(),routes.begin(),routes.end());auto&c=coverage(i,s,room);c.routes=true;c.events=true;return true;
}
namespace {
constexpr size_t MaxResource=64u<<20;
Bytes read(const std::filesystem::path&p,uint64_t at,size_t count){if(count>MaxResource)return {};std::ifstream f(p,std::ios::binary);if(!f)return {};f.seekg(std::streamoff(at));Bytes b(count);if(!f.read(reinterpret_cast<char*>(b.data()),std::streamsize(count)))return {};return b;}
Bytes unyaz(const Bytes&b){if(b.size()<4||std::memcmp(b.data(),"Yaz0",4))return b;if(b.size()<16)return {};size_t size=u32(b,4);if(size>MaxResource)return {};Bytes out;out.reserve(size);size_t p=16;while(out.size()<size){if(p>=b.size())return {};uint8_t code=b[p++];for(unsigned k=0;k<8&&out.size()<size;++k){if(code&(128>>k)){if(p>=b.size())return {};out.push_back(b[p++]);}else{if(!range(b,p,2))return {};uint8_t a=b[p++],c=b[p++];size_t dist=((a&15)<<8|c)+1,n=a>>4;if(!n){if(p>=b.size())return {};n=b[p++]+18;}else n+=2;if(dist>out.size()||n>size-out.size())return {};for(size_t j=0;j<n;++j)out.push_back(out[out.size()-dist]);}}}return out;}
struct Member {std::string name;uint32_t start,end;};
bool sarc(const Bytes&b,std::vector<Member>&out,uint32_t&base,bool header_only=false){if(b.size()<32||std::memcmp(b.data(),"SARC",4)||u16(b,6)!=0xFEFF)return false;size_t h=u16(b,4);base=u32(b,12);if(!range(b,h,12)||std::memcmp(b.data()+h,"SFAT",4))return false;unsigned n=u16(b,h+6);size_t nodes=h+u16(b,h+4),nt=nodes+uint64_t(n)*16;if(!range(b,nodes,uint64_t(n)*16)||!range(b,nt,8)||std::memcmp(b.data()+nt,"SFNT",4))return false;size_t names=nt+u16(b,nt+4);for(unsigned j=0;j<n;++j){size_t p=nodes+j*16;uint32_t attr=u32(b,p+4),start=u32(b,p+8),end=u32(b,p+12);if(!(attr&0x01000000)||end<start||(!header_only&&!range(b,uint64_t(base)+start,end-start)))return false;size_t name=names+uint64_t(attr&0xFFFFFF)*4;if(name>=b.size())return false;size_t endname=name;while(endname<b.size()&&b[endname])++endname;if(endname==b.size()||endname-name>256)return false;out.push_back({std::string(reinterpret_cast<const char*>(b.data()+name),endname-name),start,end});}return true;}
Bytes archive(const std::string&dir,const std::string&name){namespace fs=std::filesystem;std::error_code ec;auto path=fs::path(dir)/"content/Common/Stage"/name;if(fs::is_regular_file(path,ec)){uint64_t n=fs::file_size(path,ec);if(!ec&&n<=MaxResource)return unyaz(read(path,0,size_t(n)));return {};}
 const auto packdir=fs::path(dir)/"content/Common/Pack";for(const auto&e:fs::directory_iterator(packdir,ec)){if(e.path().extension()!=".pack")continue;auto size=fs::file_size(e.path(),ec);if(ec)continue;auto header=read(e.path(),0,size_t(std::min<uint64_t>(size,1u<<20)));std::vector<Member> ms;uint32_t base=0;if(!sarc(header,ms,base,true))continue;for(const auto&m:ms)if(fs::path(m.name).filename()==name&&uint64_t(base)+m.end<=size)return unyaz(read(e.path(),uint64_t(base)+m.start,m.end-m.start));}return {};
}
bool relative(const Bytes&b,size_t at,size_t&result){if(!range(b,at,4))return false;const int64_t p=int64_t(at)+int32_t(u32(b,at));if(!u32(b,at)||p<0||uint64_t(p)>=b.size())return false;result=size_t(p);return true;}
bool native_members(const Bytes&arc,std::map<std::string,Bytes>&out){std::vector<Member> ms;uint32_t base=0;if(!sarc(arc,ms,base))return false;for(const auto&m:ms){Bytes b(arc.begin()+base+m.start,arc.begin()+base+m.end);auto extension=std::filesystem::path(m.name).extension();if(extension==".dzr"||extension==".dzs"||extension==".dzb"){if(out.contains(m.name))return false;out.emplace(m.name,std::move(b));continue;}if(extension!=".bfres")continue;
 if(b.size()<0x6c||std::memcmp(b.data(),"FRES",4)||u16(b,8)!=0xFEFF)return false;size_t dict;if(!relative(b,0x4c,dict))continue;if(!range(b,dict,8))return false;unsigned n=u32(b,dict+4);if(n>4096||!range(b,dict+8,uint64_t(n+1)*16))return false;for(unsigned j=0;j<n;++j){size_t entry=dict+24+j*16,name,data;if(!relative(b,entry+8,name)||!relative(b,entry+12,data))return false;size_t end=name;while(end<b.size()&&b[end])++end;if(end==b.size()||end-name>256)return false;std::string key(reinterpret_cast<const char*>(b.data()+name),end-name);auto ext=std::filesystem::path(key).extension();if(ext!=".dzr"&&ext!=".dzs"&&ext!=".dzb")continue;size_t bytes;if(!relative(b,data,bytes)||!range(b,data,8))return false;unsigned count=u32(b,data+4);if(!range(b,bytes,count)||out.contains(key))return false;out.emplace(key,Bytes(b.begin()+bytes,b.begin()+bytes+count));}
 }return true;
}
}
Index load_native_room(const std::string&dir,const std::string&s,int room){
 Index out;auto unknown=[&](const std::string&why){auto& c=coverage(out,s,room);c.collision=c.routes=c.events=false;c.reason=why;return out;};
 if(s.empty()||s.size()>8||s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos||room<0||room>63)return unknown("Invalid stage or room");
 try{
 std::map<std::string,Bytes> stage,local;if(!native_members(archive(dir,s+"_Stage.szs"),stage))return unknown("Stage archive is missing or malformed");if(!native_members(archive(dir,s+"_Room"+std::to_string(room)+".szs"),local))return unknown("Room archive is missing or malformed");
 std::vector<std::pair<int,Point>> room_origins;Point translation{};float yaw=0;bool stage_layout=false,room_layout=false,room_collision=false,multi_seen=false,multi_found=false;std::string why;
 for(const auto&[name,b]:stage){auto ext=std::filesystem::path(name).extension();if(ext!=".dzs"&&ext!=".dzr")continue;std::vector<Chunk> cs;if(!chunks(b,cs))return unknown("Stage layout is malformed");for(const auto&c:cs)if(c.tag=="MULT"){multi_seen=true;if(c.count>64||!range(b,c.offset,uint64_t(c.count)*12))return unknown("Room transform table is malformed");for(uint32_t j=0;j<c.count;++j){size_t p=c.offset+j*12;Point origin{f32(b,p),0,f32(b,p+4)};if(!finite(origin))return unknown("Non-finite room transform");room_origins.push_back({b[p+10],origin});if(b[p+10]==room){if(multi_found)return unknown("Duplicate room transform");translation={f32(b,p),0,f32(b,p+4)};yaw=int16_t(u16(b,p+8))*(360.f/65536.f);multi_found=true;}}}}
 if(multi_seen&&!multi_found)return unknown("Room transform is unavailable");
 // Sea MULT origins define the square grid. Restrict admission to this room's
 // cell instead of pretending to have collision coverage from adjacent rooms.
 if(s=="sea"){
  if(!multi_found)return unknown("Sea room grid is unavailable");
  float xl=std::numeric_limits<float>::infinity(),xr=xl,zl=xl,zr=xl;
  for(const auto&[other,p]:room_origins){if(other==room)continue;float dx=p.x-translation.x,dz=p.z-translation.z;if(std::fabs(dz)<.01f){if(dx<0)xl=std::min(xl,-dx/2);if(dx>0)xr=std::min(xr,dx/2);}if(std::fabs(dx)<.01f){if(dz<0)zl=std::min(zl,-dz/2);if(dz>0)zr=std::min(zr,dz/2);}}
  if(!std::isfinite(xl))xl=xr;if(!std::isfinite(xr))xr=xl;if(!std::isfinite(zl))zl=zr;if(!std::isfinite(zr))zr=zl;
  if(!std::isfinite(xl)||!std::isfinite(xr)||!std::isfinite(zl)||!std::isfinite(zr))return unknown("Sea room neighbours are unavailable");
  out.regions.push_back({s,room,{{translation.x-xl,-1000000,translation.z-zl},{translation.x+xr,1000000,translation.z+zr}}});
 }
 for(const auto&[name,b]:stage){auto ext=std::filesystem::path(name).extension();if(ext==".dzb"&&!add_native_collision(out,s,-1,b,{},0,&why))return unknown(why);if(ext==".dzs"||ext==".dzr"){if(!add_native_layout(out,s,-1,b,{},0,&why))return unknown(why);stage_layout=true;}}
 for(const auto&[name,b]:local){auto ext=std::filesystem::path(name).extension();if(ext==".dzb"){if(!add_native_collision(out,s,room,b,translation,yaw,&why))return unknown(why);room_collision=true;}if(ext==".dzr"||ext==".dzs"){if(!add_native_layout(out,s,room,b,translation,yaw,&why))return unknown(why);room_layout=true;}}
 auto&c=coverage(out,s,room);c.collision=room_collision;c.routes=room_layout&&stage_layout;c.events=room_layout&&stage_layout;if(!room_collision||!room_layout||!stage_layout)c.reason="Native collision or stage/room layouts are absent";
 }catch(const std::exception&){return unknown("Unable to read native placement resources");}return out;
}
}
