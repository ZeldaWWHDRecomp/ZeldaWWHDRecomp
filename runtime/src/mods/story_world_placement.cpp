#include "story_world_placement.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
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
std::string lower(std::string v){for(char&c:v)c=char(std::tolower((unsigned char)c));return v;}
std::string article(const char*w){return std::string(std::strchr("aeiou",w[0])?"an ":"a ")+w;}
bool starts(const std::string&v,std::initializer_list<const char*> prefixes){for(const char*p:prefixes)if(v.rfind(p,0)==0)return true;return false;}
// a walking route of the room's layout: RPAT (the room's) and PATH (the stage's) carry villagers and animals
constexpr float kSeaLevel=20;
bool sea_route(const ProtectedRoute&r){for(const auto&p:r.points)if(p.y>kSeaLevel)return false;return !r.points.empty();}
// an object's own triangles against what is kept clear (issue #281; defined below with the solids)
// The object's triangles by where they are (a grid over X/Z, at most 64 x 64 cells): a query visits only the triangles
// whose bounds meet its own, each once, so a model of a few thousand triangles stays cheap against a room's layout.
struct Near {
 const Solid&s;std::vector<Bounds> tb;float x0=0,z0=0,cell=1;int nx=1,nz=1;std::vector<std::vector<uint32_t>> cells;mutable std::vector<uint32_t> seen;mutable uint32_t tick=0;
 explicit Near(const Solid&solid):s(solid){
  tb.reserve(s.triangles.size());for(const auto&t:s.triangles){Bounds b{s.vertices[t[0]],s.vertices[t[0]]};for(unsigned k=1;k<3;++k){const Point p=s.vertices[t[k]];b.minimum={std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};b.maximum={std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};}tb.push_back(b);}
  x0=s.bounds.minimum.x;z0=s.bounds.minimum.z;const double w=double(s.bounds.maximum.x)-x0,d=double(s.bounds.maximum.z)-z0;
  cell=float(std::max({w/64,d/64,100.0}));if(!std::isfinite(cell))cell=1e9f;nx=std::clamp(int(w/cell)+1,1,64);nz=std::clamp(int(d/cell)+1,1,64);
  cells.resize(size_t(nx)*nz);seen.assign(tb.size(),0);
  for(uint32_t j=0;j<tb.size();++j){int a,b,c,e;range(tb[j],a,b,c,e);for(int z=c;z<=e;++z)for(int x=a;x<=b;++x)cells[size_t(z)*nx+x].push_back(j);}
 }
 void range(const Bounds&q,int&a,int&b,int&c,int&e)const{auto ix=[&](double v,int n,double o){return std::clamp(int(std::floor((v-o)/cell)),0,n-1);};a=ix(q.minimum.x,nx,x0);b=ix(q.maximum.x,nx,x0);c=ix(q.minimum.z,nz,z0);e=ix(q.maximum.z,nz,z0);}
 // f(triangle index) for each triangle whose bounds meet q; true from f stops the visit (and is returned)
 template<class F> bool visit(const Bounds&q,F&&f)const{
  if(q.minimum.x>s.bounds.maximum.x||q.maximum.x<s.bounds.minimum.x||q.minimum.y>s.bounds.maximum.y||q.maximum.y<s.bounds.minimum.y||q.minimum.z>s.bounds.maximum.z||q.maximum.z<s.bounds.minimum.z)return false;
  if(++tick==0){std::fill(seen.begin(),seen.end(),0);tick=1;}
  int a,b,c,e;range(q,a,b,c,e);
  for(int z=c;z<=e;++z)for(int x=a;x<=b;++x)for(uint32_t j:cells[size_t(z)*nx+x]){if(seen[j]==tick)continue;seen[j]=tick;const Bounds&t=tb[j];
   if(t.minimum.x>q.maximum.x||t.maximum.x<q.minimum.x||t.minimum.y>q.maximum.y||t.maximum.y<q.minimum.y||t.minimum.z>q.maximum.z||t.maximum.z<q.minimum.z)continue;
   if(f(j))return true;}
  return false;
 }
};
bool solid_touches_box(const Near&,const Bounds&);
bool solid_in_column(const Near&,Point centre,double reach,double low,double high);
bool solid_near_segment(const Near&,Point a,Point b,double reach);
unsigned solid_crossings(const Near&,const std::vector<const ProtectedBounds*>&,std::string&first);
std::vector<Conflict> building_conflicts(const std::string&s,int room,const Bounds&b,const Index&i,float margin,const Near* solid);
const char* route_words(const ProtectedRoute&r){if(r.user=="boat")return "boat route";return r.id.rfind("PATH",0)==0||r.id.rfind("RPAT",0)==0?"villager route":"route";}
bool chunks(const Bytes&b,std::vector<Chunk>&out){if(!range(b,0,4))return false;uint32_t n=u32(b,0);if(!n||n>256||!range(b,4,uint64_t(n)*12))return false;for(unsigned j=0;j<n;++j){size_t p=4+j*12;std::string tag(reinterpret_cast<const char*>(b.data()+p),4);for(unsigned char c:tag)if(!std::isalnum(c))return false;uint32_t at=u32(b,p+8);if(at<4+n*12||at>b.size())return false;out.push_back({tag,u32(b,p+4),at});}return true;}
}
bool valid(const Bounds&b){return finite(b.minimum)&&finite(b.maximum)&&b.minimum.x<=b.maximum.x&&b.minimum.y<=b.maximum.y&&b.minimum.z<=b.maximum.z;}
bool transform_bounds(const Bounds&b,Point t,Point r,Point s,Bounds&out){if(!valid(b)||!finite(t)||!finite(r)||!finite(s)||s.x==0||s.y==0||s.z==0)return false;Bounds result=empty();for(unsigned k=0;k<8;++k)include(result,transformed({k&1?b.maximum.x:b.minimum.x,k&2?b.maximum.y:b.minimum.y,k&4?b.maximum.z:b.minimum.z},t,r,s));if(!valid(result))return false;out=result;return true;}
std::vector<Conflict> check(const std::string&s,int room,const Bounds&b,const Index&i,float pad,const Solid* own){
 std::optional<Near> grid;if(own)grid.emplace(*own);const Near* solid=grid?&*grid:nullptr;
 std::vector<Conflict> out;if(s.empty()||room<0||room>63||!valid(b)||!std::isfinite(pad)||pad<0){out.push_back({"invalid","","Invalid world bounds or clearance"});return out;}
 const Coverage*c=nullptr;for(const auto&v:i.coverage)if(v.stage==s&&v.room==room){c=&v;break;}
 if(!c||!c->collision||!c->routes||!c->events)out.push_back({"coverage","",c&&!c->reason.empty()?c->reason:"Native collision, route or event coverage is unavailable for this room"});
 for(const auto&v:i.regions)if(v.stage==s&&v.room==room){if(!valid(v.bounds)||b.minimum.x<v.bounds.minimum.x||b.minimum.y<v.bounds.minimum.y||b.minimum.z<v.bounds.minimum.z||b.maximum.x>v.bounds.maximum.x||b.maximum.y>v.bounds.maximum.y||b.maximum.z>v.bounds.maximum.z)out.push_back({"coverage","room-boundary","Placement extends beyond the indexed room; neighbouring room coverage is required"});}
 // the room's walls and ground: one finding for all the triangles it goes into (resting on a floor is fine)
 unsigned walls=0;std::string first_wall;std::vector<const ProtectedBounds*> near_walls;
 for(const auto&v:i.bounds)if(v.stage==s&&(v.room<0||v.room==room)){if(!valid(v.bounds)){out.push_back({"coverage",v.id,"Protected bounds are invalid"});continue;}
  if(v.kind=="collision"&&v.bounds.maximum.y<=b.minimum.y+.01f)continue;
  if(!overlap(b,v.bounds,pad))continue;
  // with the object's own triangles: the room's triangles near it, checked together below
  if(v.kind=="collision"&&solid&&v.triangle){near_walls.push_back(&v);continue;}
  if(v.kind=="collision"){if(!walls++)first_wall=v.id;continue;}
  if(solid){Bounds reach=v.bounds;reach.minimum={reach.minimum.x-pad,reach.minimum.y-pad,reach.minimum.z-pad};reach.maximum={reach.maximum.x+pad,reach.maximum.y+pad,reach.maximum.z+pad};if(!solid_touches_box(*solid,reach))continue;}
  out.push_back({v.kind,v.id,v.kind=="world_object"?"another mod's world object":v.kind=="ship"?"the King of Red Lions":"protected "+v.kind+" bounds",v.kind=="world_object"?v.id:""});}
 // what the room's layout needs: villagers and their routes, chests, doors, items, switches, cutscene areas, start
 // points; then the walls and ground (the layout's reasons say more to a mod maker, so they come first)
 const auto layout=building_conflicts(s,room,b,i,pad,solid);out.insert(out.end(),layout.begin(),layout.end());
 if(!near_walls.empty()){std::string first;if(const unsigned n=solid_crossings(*solid,near_walls,first)){if(!walls)first_wall=first;walls+=n;}}
 if(walls)out.push_back({"collision",first_wall,"the island's walls or ground ("+std::to_string(walls)+(walls==1?" triangle)":" triangles)"),""});
 return out;
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
   const Point a=v.points[k],e=v.points[(k+1)%v.points.size()];const double reach=double(pad)+v.radius;
   // a route far above or below the body (a cliff path over a raft) is not in its way, as in check()
   if(std::min(a.y,e.y)>current.maximum.y+reach||std::max(a.y,e.y)<current.minimum.y-reach)continue;
   const char*words=route_words(v);
   consider(projected_hit(current,remaining,a,e,reach,true,{words,v.id,std::string("Drift reached ")+article(words)+" of the island",v.id}));
  }
  // the layout's own places (story_world_placement.h building_extent), as squares around their columns
  for(const auto&e:i.layout)if(e.stage==s&&(e.room<0||e.room==room)){
   const Extent x=building_extent(e);if(x.keep==Keep::None)continue;
   if(double(current.minimum.y)>double(e.position.y)+x.above+pad||double(current.maximum.y)<double(e.position.y)-x.below-pad)continue;
   const double reach=double(x.radius)+pad;
   if(double(current.minimum.x)+std::min(0.0,double(remaining.x))>e.position.x+reach||double(current.maximum.x)+std::max(0.0,double(remaining.x))<e.position.x-reach||double(current.minimum.z)+std::min(0.0,double(remaining.z))>e.position.z+reach||double(current.maximum.z)+std::max(0.0,double(remaining.z))<e.position.z-reach)continue;
   const char*words=keep_words(x.keep);
   consider(projected_hit(current,remaining,e.position,e.position,0,false,{words,e.tag+":"+e.name+":"+std::to_string(e.index),std::string("Drift reached ")+article(words)+" of the island",e.name},reach,reach));
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
unsigned table_stride(const std::string&tag){
 // dStage chunk handlers: actors, objects, treasure and start points are 0x20 bytes; the scaled tables
 // (SCOB, TGSC, and the doors DOOR/TGDR: dStage_tgscInfoInit, DoorInfo) are 0x24 with a u8[3] scale at 0x20.
 // Layered tables carry the layer as their last letter (0-9, a, b).
 if(tag.size()!=4)return 0;const char last=tag[3];const bool layer=(last>='0'&&last<='9')||last=='a'||last=='b';
 if(tag=="ACTR"||tag=="TGOB"||tag=="TRES"||tag=="PLYR"||((tag.rfind("ACT",0)==0||tag.rfind("TRE",0)==0)&&layer))return 32;
 if(tag=="SCOB"||tag=="TGSC"||tag=="TGDR"||tag=="DOOR"||(tag.rfind("SCO",0)==0&&layer))return 36;
 return 0;
}
// The room's collision and layout are in world space as stored (also a sea room's: its MULT entry is the room's
// place on the sea chart and the minimap's turn, not a transform of its data), so nothing is moved here.
bool add_native_collision(Index&i,const std::string&s,int room,const Bytes&b,std::string*w){
 if(!range(b,0,0x34))return fail(w,"Invalid native collision header");
 const uint32_t nv=u32(b,0),vp=u32(b,4),nt=u32(b,8),tp=u32(b,12);
 if(nv>65535||nt>1000000||!range(b,vp,uint64_t(nv)*12)||!range(b,tp,uint64_t(nt)*10)||((nv==0)!=(nt==0)))return fail(w,"Invalid native collision tables");
 std::vector<Point> vertices;vertices.reserve(nv);for(uint32_t j=0;j<nv;++j){auto p=point(b,vp+j*12);if(!finite(p))return fail(w,"Non-finite native collision vertex");vertices.push_back(p);}
 // water and lava surfaces (a group whose info has 0x100 water or 0x200 lava: the sea plane every island square carries,
 // "umi"/"mizuba" under Water_00) are not ground or walls: a floating object or a pier may reach into them
 std::vector<bool> liquid;
 if(range(b,0x20,8)){const uint32_t ng=u32(b,0x20),gp=u32(b,0x24);if(ng&&ng<=65535&&range(b,gp,uint64_t(ng)*0x34)){liquid.resize(ng);for(uint32_t g=0;g<ng;++g)liquid[g]=(u32(b,gp+g*0x34+0x30)&0x300)!=0;}}
 std::vector<ProtectedBounds> added;added.reserve(nt);for(uint32_t j=0;j<nt;++j){const uint16_t group=u16(b,tp+j*10+8);if(group<liquid.size()&&liquid[group])continue;Bounds box=empty();std::array<Point,3> corners;for(unsigned k=0;k<3;++k){uint16_t index=u16(b,tp+j*10+k*2);if(index>=nv)return fail(w,"Invalid native triangle vertex");include(box,vertices[index]);corners[k]=vertices[index];}added.push_back({s,room,"collision","native-triangle-"+std::to_string(j),box,true,corners});}
 i.bounds.insert(i.bounds.end(),added.begin(),added.end());coverage(i,s,room).collision=true;return true;
}
bool add_native_layout(Index&i,const std::string&s,int room,const Bytes&b,std::string*w){
 std::vector<Chunk> cs;if(!chunks(b,cs))return fail(w,"Invalid native room layout");
 std::vector<ProtectedRoute> routes;std::vector<LayoutEntry> layout;
 for(const auto&c:cs){
  if(const unsigned stride=table_stride(c.tag)){
   if(c.count>65535||!range(b,c.offset,uint64_t(c.count)*stride))return fail(w,"Invalid native actor table");
   for(uint32_t j=0;j<c.count;++j){const size_t at=c.offset+j*stride;LayoutEntry e{s,room,c.tag,std::string(reinterpret_cast<const char*>(b.data()+at),strnlen(reinterpret_cast<const char*>(b.data()+at),8)),point(b,at+12),{},stride==36,j};
    e.params=u32(b,at+8);e.angle_x=int16_t(u16(b,at+24));if(!finite(e.position))return fail(w,"Invalid native layout position");if(e.scaled)for(unsigned k=0;k<3;++k)e.scale[k]=b[at+32+k];layout.push_back(std::move(e));}}
  if(c.tag=="RPAT"||c.tag=="PATH"){
   if(c.count>65535||!range(b,c.offset,uint64_t(c.count)*12))return fail(w,"Invalid native path table");for(uint32_t j=0;j<c.count;++j){size_t at=c.offset+j*12;uint16_t count=u16(b,at);uint32_t point_offset=u32(b,at+8),points=0;bool in_points=false;for(const auto& pc:cs)if(((c.tag=="RPAT"&&pc.tag=="RPPN")||(c.tag=="PATH"&&pc.tag=="PPNT"))&&range(b,pc.offset,uint64_t(pc.count)*16)&&point_offset%16==0&&uint64_t(point_offset)+uint64_t(count)*16<=uint64_t(pc.count)*16){points=pc.offset+point_offset;in_points=true;}if(count>4096||!range(b,points,uint64_t(count)*16)||(count&&!in_points))return fail(w,"Invalid native path points");ProtectedRoute route{s,room,c.tag+":"+std::to_string(j),{},kRouteHalfWidth,b[at+5]!=0};for(unsigned k=0;k<count;++k){auto p=point(b,points+k*16+4);if(!finite(p))return fail(w,"Invalid native path coordinate");route.points.push_back(p);}if(!route.points.empty())routes.push_back(std::move(route));}
  }
 }
 // a boat of the room's layout (d_a_obj_ikada getArg: type params & 15; type 4 names its path in params >> 16, the
 // others in the high byte of the home angle's x) sails its route: said as a boat route, not a villager's
 for(const auto&e:layout){if(lower(e.name).rfind("ikada",0)!=0||e.tag=="PLYR")continue;
  const unsigned path=(e.params&15)==4?unsigned(e.params>>16):unsigned(uint16_t(e.angle_x)>>8);
  for(auto&r:routes)if(r.id=="RPAT:"+std::to_string(path))r.user="boat";}
 i.layout.insert(i.layout.end(),layout.begin(),layout.end());i.routes.insert(i.routes.end(),routes.begin(),routes.end());auto&c=coverage(i,s,room);c.routes=true;c.events=true;return true;
}
namespace {
constexpr size_t MaxResource=64u<<20;
Bytes read(const std::filesystem::path&p,uint64_t at,size_t count){if(count>MaxResource)return {};std::ifstream f(p,std::ios::binary);if(!f)return {};f.seekg(std::streamoff(at));Bytes b(count);if(!f.read(reinterpret_cast<char*>(b.data()),std::streamsize(count)))return {};return b;}
Bytes unyaz(const Bytes&b){if(b.size()<4||std::memcmp(b.data(),"Yaz0",4))return b;if(b.size()<16)return {};size_t size=u32(b,4);if(size>MaxResource)return {};Bytes out;out.reserve(size);size_t p=16;while(out.size()<size){if(p>=b.size())return {};uint8_t code=b[p++];for(unsigned k=0;k<8&&out.size()<size;++k){if(code&(128>>k)){if(p>=b.size())return {};out.push_back(b[p++]);}else{if(!range(b,p,2))return {};uint8_t a=b[p++],c=b[p++];size_t dist=((a&15)<<8|c)+1,n=a>>4;if(!n){if(p>=b.size())return {};n=b[p++]+18;}else n+=2;if(dist>out.size()||n>size-out.size())return {};for(size_t j=0;j<n;++j)out.push_back(out[out.size()-dist]);}}}return out;}
struct Member {std::string name;uint32_t start,end;};
bool sarc(const Bytes&b,std::vector<Member>&out,uint32_t&base,bool header_only=false){if(b.size()<32||std::memcmp(b.data(),"SARC",4)||u16(b,6)!=0xFEFF)return false;size_t h=u16(b,4);base=u32(b,12);if(!range(b,h,12)||std::memcmp(b.data()+h,"SFAT",4))return false;unsigned n=u16(b,h+6);size_t nodes=h+u16(b,h+4),nt=nodes+uint64_t(n)*16;if(!range(b,nodes,uint64_t(n)*16)||!range(b,nt,8)||std::memcmp(b.data()+nt,"SFNT",4))return false;size_t names=nt+u16(b,nt+4);for(unsigned j=0;j<n;++j){size_t p=nodes+j*16;uint32_t attr=u32(b,p+4),start=u32(b,p+8),end=u32(b,p+12);if(!(attr&0x01000000)||end<start||(!header_only&&!range(b,uint64_t(base)+start,end-start)))return false;size_t name=names+uint64_t(attr&0xFFFFFF)*4;if(name>=b.size())return false;size_t endname=name;while(endname<b.size()&&b[endname])++endname;if(endname==b.size()||endname-name>256)return false;out.push_back({std::string(reinterpret_cast<const char*>(b.data()+name),endname-name),start,end});}return true;}
std::function<std::string(const std::string&)> g_stage_source;
Bytes archive(const std::string&dir,const std::string&name){namespace fs=std::filesystem;std::error_code ec;auto path=fs::path(dir)/"content/Common/Stage"/name;
 if(g_stage_source)if(auto local=g_stage_source(name);!local.empty())path=local;if(fs::is_regular_file(path,ec)){uint64_t n=fs::file_size(path,ec);if(!ec&&n<=MaxResource)return unyaz(read(path,0,size_t(n)));return {};}
 const auto packdir=fs::path(dir)/"content/Common/Pack";for(const auto&e:fs::directory_iterator(packdir,ec)){if(e.path().extension()!=".pack")continue;auto size=fs::file_size(e.path(),ec);if(ec)continue;auto header=read(e.path(),0,size_t(std::min<uint64_t>(size,1u<<20)));std::vector<Member> ms;uint32_t base=0;if(!sarc(header,ms,base,true))continue;for(const auto&m:ms)if(fs::path(m.name).filename()==name&&uint64_t(base)+m.end<=size)return unyaz(read(e.path(),uint64_t(base)+m.start,m.end-m.start));}return {};
}
bool relative(const Bytes&b,size_t at,size_t&result){if(!range(b,at,4))return false;const int64_t p=int64_t(at)+int32_t(u32(b,at));if(!u32(b,at)||p<0||uint64_t(p)>=b.size())return false;result=size_t(p);return true;}
bool native_members(const Bytes&arc,std::map<std::string,Bytes>&out){std::vector<Member> ms;uint32_t base=0;if(!sarc(arc,ms,base))return false;for(const auto&m:ms){Bytes b(arc.begin()+base+m.start,arc.begin()+base+m.end);auto extension=std::filesystem::path(m.name).extension();if(extension==".dzr"||extension==".dzs"||extension==".dzb"){if(out.contains(m.name))return false;out.emplace(m.name,std::move(b));continue;}if(extension!=".bfres")continue;
 if(b.size()<0x6c||std::memcmp(b.data(),"FRES",4)||u16(b,8)!=0xFEFF)return false;size_t dict;if(!relative(b,0x4c,dict))continue;if(!range(b,dict,8))return false;unsigned n=u32(b,dict+4);if(n>4096||!range(b,dict+8,uint64_t(n+1)*16))return false;for(unsigned j=0;j<n;++j){size_t entry=dict+24+j*16,name,data;if(!relative(b,entry+8,name)||!relative(b,entry+12,data))return false;size_t end=name;while(end<b.size()&&b[end])++end;if(end==b.size()||end-name>256)return false;std::string key(reinterpret_cast<const char*>(b.data()+name),end-name);auto ext=std::filesystem::path(key).extension();if(ext!=".dzr"&&ext!=".dzs"&&ext!=".dzb")continue;size_t bytes;if(!relative(b,data,bytes)||!range(b,data,8))return false;unsigned count=u32(b,data+4);if(!range(b,bytes,count)||out.contains(key))return false;out.emplace(key,Bytes(b.begin()+bytes,b.begin()+bytes+count));}
 }return true;
}
}
void set_stage_archive_source(std::function<std::string(const std::string&)> source){g_stage_source=std::move(source);}
Index load_native_room(const std::string&dir,const std::string&s,int room){
 Index out;auto unknown=[&](const std::string&why){auto& c=coverage(out,s,room);c.collision=c.routes=c.events=false;c.reason=why;return out;};
 if(s.empty()||s.size()>8||s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos||room<0||room>63)return unknown("Invalid stage or room");
 try{
 std::map<std::string,Bytes> stage,local;if(!native_members(archive(dir,s+"_Stage.szs"),stage))return unknown("Stage archive is missing or malformed");if(!native_members(archive(dir,s+"_Room"+std::to_string(room)+".szs"),local))return unknown("Room archive is missing or malformed");
 std::vector<std::pair<int,Point>> room_origins;Point origin{};bool stage_layout=false,room_layout=false,room_collision=false,multi_found=false;std::string why;
 // Sea MULT origins define the square grid. Restrict admission to this room's
 // cell instead of pretending to have collision coverage from adjacent rooms.
 if(s=="sea"){
  for(const auto&[name,b]:stage){auto ext=std::filesystem::path(name).extension();if(ext!=".dzs"&&ext!=".dzr")continue;std::vector<Chunk> cs;if(!chunks(b,cs))return unknown("Stage layout is malformed");for(const auto&c:cs)if(c.tag=="MULT"){if(c.count>64||!range(b,c.offset,uint64_t(c.count)*12))return unknown("Room grid table is malformed");for(uint32_t j=0;j<c.count;++j){size_t p=c.offset+j*12;Point o{f32(b,p),0,f32(b,p+4)};if(!finite(o))return unknown("Non-finite room grid entry");room_origins.push_back({b[p+10],o});if(b[p+10]==room){if(multi_found)return unknown("Duplicate room grid entry");origin=o;multi_found=true;}}}}
  if(!multi_found)return unknown("Sea room grid is unavailable");
  float xl=std::numeric_limits<float>::infinity(),xr=xl,zl=xl,zr=xl;
  for(const auto&[other,p]:room_origins){if(other==room)continue;float dx=p.x-origin.x,dz=p.z-origin.z;if(std::fabs(dz)<.01f){if(dx<0)xl=std::min(xl,-dx/2);if(dx>0)xr=std::min(xr,dx/2);}if(std::fabs(dx)<.01f){if(dz<0)zl=std::min(zl,-dz/2);if(dz>0)zr=std::min(zr,dz/2);}}
  if(!std::isfinite(xl))xl=xr;if(!std::isfinite(xr))xr=xl;if(!std::isfinite(zl))zl=zr;if(!std::isfinite(zr))zr=zl;
  if(!std::isfinite(xl)||!std::isfinite(xr)||!std::isfinite(zl)||!std::isfinite(zr))return unknown("Sea room neighbours are unavailable");
  out.regions.push_back({s,room,{{origin.x-xl,-1000000,origin.z-zl},{origin.x+xr,1000000,origin.z+zr}}});
 }
 for(const auto&[name,b]:stage){auto ext=std::filesystem::path(name).extension();if(ext==".dzb"&&!add_native_collision(out,s,-1,b,&why))return unknown(why);if(ext==".dzs"||ext==".dzr"){if(!add_native_layout(out,s,-1,b,&why))return unknown(why);stage_layout=true;}}
 for(const auto&[name,b]:local){auto ext=std::filesystem::path(name).extension();if(ext==".dzb"){if(!add_native_collision(out,s,room,b,&why))return unknown(why);room_collision=true;}if(ext==".dzr"||ext==".dzs"){if(!add_native_layout(out,s,room,b,&why))return unknown(why);room_layout=true;}}
 auto&c=coverage(out,s,room);c.collision=room_collision;c.routes=room_layout&&stage_layout;c.events=room_layout&&stage_layout;if(!room_collision||!room_layout||!stage_layout)c.reason="Native collision or stage/room layouts are absent";
 }catch(const std::exception&){return unknown("Unable to read native placement resources");}return out;
}

// ---- what the room's layout needs kept clear (world objects and island buildings alike)

Extent building_extent(const LayoutEntry&e){
 const std::string n=lower(e.name);
 // things a building may cover: grass, flowers, trees and palms, birds, crabs, small stones, pigs
 if(starts(n,{"kusa","flwr","flower","pflower","pflwr","bflower","swood","lwood","woodb","oyashi","kamome","kani","koisi","pig"}))return {Keep::None,0,0,0};
 // pots, barrels and signs: a building may stand where they were (nothing needs them there)
 if(starts(n,{"kotubo","ootubo","ktaru","kanban"}))return {Keep::None,0,0,0};
 // tags that need no place of their own (measured on Outset, Windfall, Dragon Roost and the Private Oasis, where
 // they refused most open ground): the island LOD markers of the sea stage (LODnn), light and weather areas
 // (kytag, ky_tag), sound and music (TagSo, TagMSo, NpcSo), wind, the seagull possession and restart tags (WindTag,
 // HyoiKam, ReTag), the island name (TagIsl), attention, hint and message areas (AttTag, TagHt, TagMsg: text when
 // Link walks in, nothing he has to reach), the pigs' digging spots (TagKb), salvage points at sea (Salvag, SwSlvg),
 // item attention marks (ITat), invisible walls (Akabe), the sea's corner markers (Com_A), and the GBA (Tingle Tuner)
 // switches, which HD reduces to bomb-hit switches nothing walks to (d_a_agbsw0)
 if(starts(n,{"lod","kytag","ky_tag","tagso","tagmso","npcso","windtag","hyoikam","retag","tagisl","atttag","taght","tagmsg","tagkb","tagmk","salvag","swslvg","itat","akabe","com_a","agb"})||n=="sea"||n=="ship")return {Keep::None,0,0,0};
 // where Link arrives (a door's or a warp's start point): room for him to stand and step away
 if(e.tag=="PLYR")return {Keep::Start,100,50,250};
 if(e.tag.rfind("TRE",0)==0||starts(n,{"takara"}))return {Keep::Chest,120,50,250};
 if(e.tag=="TGDR"||e.tag=="DOOR"||starts(n,{"knob","door"}))return {Keep::Door,250,50,450};
 // event triggers Link walks into (d_a_tag_event, d_a_tag_evsw, the area switch SW_C00): a small one whole, a large
 // one by its core of 300 units (a building over part of a wide area leaves the rest of it to walk into); its height
 // from the scale (d_a_tag_event huntRange: radius scale x * 10, height scale y * 10)
 if(starts(n,{"tagev","evsw","sw_c0"})){
  const float r=e.scaled?std::max(100.f,10.f*std::max(e.scale[0],e.scale[2])):150,h=e.scaled?std::max(100.f,10.f*e.scale[1]):300;return {Keep::Event,std::min(r,300.f),h,h};}
 // a loose item: only what a player would miss under a building (a piece of heart, a heart container; the item
 // number is the low byte of its params); rupees and hearts lying about may be covered
 if(n=="item"){const unsigned number=e.params&0xFF;if(number!=0x07&&number!=0x08)return {Keep::None,0,0,0};return {Keep::Item,80,50,200};}
 if(starts(n,{"item"}))return {Keep::Item,80,50,200};
 if(starts(n,{"swhit","kbota","osw","hsw","swtact"}))return {Keep::Switch,150,50,250};
 // the game's villagers and other characters: a capital, a small letter and a digit ("Uo1", "Ko1", "Ls1")
 if(e.name.size()==3&&std::isupper((unsigned char)e.name[0])&&std::islower((unsigned char)e.name[1])&&std::isdigit((unsigned char)e.name[2]))return {Keep::Villager,100,50,300};
 // small props a building would swallow (pots, barrels, signs, stakes, posts, lanterns, buckets, flags, enemies)
 if(starts(n,{"kui","tpost","bonbori","lamp","okioke","piwa","puti","ksaku","mtflag","trflag","c_green","c_red","c_blue","bbaba","keeth","p_hat","fdai"}))return {Keep::Object,60,50,250};
 // anything else: an object of unknown size (a default; a scaled one by its scale, up to 600)
 if(e.scaled){const float k=std::max({e.scale[0],e.scale[1],e.scale[2]})/10.f;if(k>0)return {Keep::Object,std::clamp(120*k,60.f,600.f),50,std::clamp(400*k,200.f,2000.f)};}
 return {Keep::Object,120,50,400};
}
const char* keep_words(Keep k){switch(k){case Keep::Villager:return "villager";case Keep::Chest:return "chest";case Keep::Door:return "door";case Keep::Item:return "item";case Keep::Switch:return "switch";case Keep::Event:return "cutscene area";case Keep::Object:return "object";case Keep::Start:return "start point";default:return "";}}
std::vector<Conflict> check_building(const std::string&s,int room,const Bounds&b,const Index&i,float margin,const Solid* own){
 std::optional<Near> grid;if(own)grid.emplace(*own);return building_conflicts(s,room,b,i,margin,grid?&*grid:nullptr);
}
namespace {
std::vector<Conflict> building_conflicts(const std::string&s,int room,const Bounds&b,const Index&i,float margin,const Near* solid){
 std::vector<Conflict> out;if(!valid(b)||!std::isfinite(margin)||margin<0){out.push_back({"invalid","","Invalid building bounds"});return out;}
 for(const auto&e:i.layout){if(e.stage!=s||(e.room>=0&&e.room!=room))continue;const Extent x=building_extent(e);if(x.keep==Keep::None)continue;
  const Bounds area{{e.position.x-x.radius,e.position.y-x.below,e.position.z-x.radius},{e.position.x+x.radius,e.position.y+x.above,e.position.z+x.radius}};
  if(!overlap(b,area,margin))continue;
  // a column's corners: keep the real circle (the box corners of a round area are not in the way)
  const float cx=std::clamp(e.position.x,b.minimum.x,b.maximum.x),cz=std::clamp(e.position.z,b.minimum.z,b.maximum.z);
  const double distance=std::hypot(double(cx)-e.position.x,double(cz)-e.position.z),reach=double(x.radius)+margin;
  if(distance>reach)continue;
  // with the object's own triangles: only when one of them comes into the column's reach (an L-shaped object's box
  // may cover a villager standing in its open corner)
  if(solid&&!solid_in_column(*solid,e.position,reach,double(e.position.y)-x.below-margin,double(e.position.y)+x.above+margin))continue;
  // how far to move: out of its reach, or (its centre under the bounds) past the nearest side and then its reach
  const double inside=std::min({double(e.position.x)-b.minimum.x,double(b.maximum.x)-e.position.x,double(e.position.z)-b.minimum.z,double(b.maximum.z)-e.position.z});
  const float move=float(std::ceil(distance>0?reach-distance+1:inside+reach+1));
  out.push_back({keep_words(x.keep),e.tag+":"+e.name+":"+std::to_string(e.index),article(keep_words(x.keep))+" of the island",e.name,move,e.position,float(reach)-margin,{}});}
 for(const auto&r:i.routes){if(r.stage!=s||(r.room>=0&&r.room!=room))continue;
  bool invalid=!std::isfinite(r.radius)||r.radius<0||r.points.empty();for(auto p:r.points)invalid|=!finite(p);
  if(invalid){out.push_back({"coverage",r.id,"Protected route data is invalid"});continue;}
  // a route all at or below sea level (boats, swimmers, seagulls' base line) is the sea's: only what reaches down
  // to the water is in its way, not a building on the island
  if(sea_route(r)&&b.minimum.y>kSeaLevel)continue;
  const char* words=route_words(r);bool hit=false;
  // with the object's own triangles: a segment in reach of the bounds is in its way when it is in reach of a triangle
  for(size_t j=0;j<r.points.size()&&!hit;++j){
   if(j+1==r.points.size()&&!r.closed&&r.points.size()!=1)break;
   const Point a=r.points[j],e=j+1<r.points.size()?r.points[j+1]:r.points[0];
   hit=segment_box(a,e,b,margin+r.radius)&&(!solid||solid_near_segment(*solid,a,e,double(margin)+r.radius));}
  // where it is: the route's point nearest the bounds (the one to keep clear of, not where the route starts)
  Point near=r.points.front();double best=1e300;const Point mid{(b.minimum.x+b.maximum.x)/2,(b.minimum.y+b.maximum.y)/2,(b.minimum.z+b.maximum.z)/2};
  for(auto p:r.points){const double dx=double(p.x)-mid.x,dz=double(p.z)-mid.z,d=dx*dx+dz*dz;if(d<best)best=d,near=p;}
  if(hit)out.push_back({words,r.id,article(words)+" of the island",r.id,0,near,r.radius,r.points});}
 return out;
}
}
// ---- solids against each other
namespace {
using Poly=std::vector<Point>;
// the part of a triangle at or above height h (0 to 4 points)
Poly clip_above(const Point (&t)[3],double h){
 Poly out;for(unsigned k=0;k<3;++k){const Point&a=t[k],&b=t[(k+1)%3];const bool ia=a.y>=h,ib=b.y>=h;if(ia)out.push_back(a);
  if(ia!=ib){const double f=(h-a.y)/(double(b.y)-a.y);out.push_back({float(a.x+(b.x-a.x)*f),float(h),float(a.z+(b.z-a.z)*f)});}}
 return out;
}
// does segment p-q cross triangle (a,b,c) (Moeller-Trumbore with the segment's ends; parallel never counts)
bool segment_triangle(Point p,Point q,Point a,Point b,Point c){
 const double d[3]={double(q.x)-p.x,double(q.y)-p.y,double(q.z)-p.z},e1[3]={double(b.x)-a.x,double(b.y)-a.y,double(b.z)-a.z},e2[3]={double(c.x)-a.x,double(c.y)-a.y,double(c.z)-a.z};
 const double h[3]={d[1]*e2[2]-d[2]*e2[1],d[2]*e2[0]-d[0]*e2[2],d[0]*e2[1]-d[1]*e2[0]};const double det=e1[0]*h[0]+e1[1]*h[1]+e1[2]*h[2];
 const double scale=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2])*std::sqrt(e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2])*std::sqrt(e2[0]*e2[0]+e2[1]*e2[1]+e2[2]*e2[2]);
 if(!(std::fabs(det)>1e-9*scale))return false;const double f=1/det,sv[3]={double(p.x)-a.x,double(p.y)-a.y,double(p.z)-a.z};
 const double u=f*(sv[0]*h[0]+sv[1]*h[1]+sv[2]*h[2]);if(u<0||u>1)return false;
 const double qv[3]={sv[1]*e1[2]-sv[2]*e1[1],sv[2]*e1[0]-sv[0]*e1[2],sv[0]*e1[1]-sv[1]*e1[0]};const double v=f*(d[0]*qv[0]+d[1]*qv[1]+d[2]*qv[2]);if(v<0||u+v>1)return false;
 const double t=f*(e2[0]*qv[0]+e2[1]*qv[1]+e2[2]*qv[2]);return t>=0&&t<=1;
}
// do two convex polygons (fans) cross: an edge of one through the other
bool polys_cross(const Poly&a,const Poly&b){
 if(a.size()<3||b.size()<3)return false;
 for(int pass=0;pass<2;++pass){const Poly&e=pass?b:a,&f=pass?a:b;
  for(size_t i=0;i<e.size();++i){const Point p=e[i],q=e[(i+1)%e.size()];for(size_t k=1;k+1<f.size();++k)if(segment_triangle(p,q,f[0],f[k],f[k+1]))return true;}}
 return false;
}
Bounds poly_bounds(const Poly&p){Bounds b=empty();for(auto q:p)include(b,q);return b;}
}
Solid place_solid(const std::vector<Point>&local,const std::vector<std::array<uint16_t,3>>&triangles,Point t,Point r,Point sc){
 Solid out;out.bounds=empty();out.vertices.reserve(local.size());for(auto p:local){out.vertices.push_back(transformed(p,t,r,sc));include(out.bounds,out.vertices.back());}
 for(const auto&tri:triangles)if(tri[0]<local.size()&&tri[1]<local.size()&&tri[2]<local.size())out.triangles.push_back(tri);
 return out;
}
bool solids_overlap(const Solid&a,const Solid&b,float step){
 if(a.triangles.empty()||b.triangles.empty()||!valid(a.bounds)||!valid(b.bounds)||!overlap(a.bounds,b.bounds,0))return false;
 // where both stand: the higher base; below it plus the step, contact is standing on (or leaning against) the other
 const double h=double(std::max(a.bounds.minimum.y,b.bounds.minimum.y))+step;
 Bounds common{{std::max(a.bounds.minimum.x,b.bounds.minimum.x),float(h),std::max(a.bounds.minimum.z,b.bounds.minimum.z)},
               {std::min(a.bounds.maximum.x,b.bounds.maximum.x),std::min(a.bounds.maximum.y,b.bounds.maximum.y),std::min(a.bounds.maximum.z,b.bounds.maximum.z)}};
 if(!valid(common))return false;
 auto clipped=[&](const Solid&s){std::vector<std::pair<Poly,Bounds>> out;for(const auto&t:s.triangles){const Point tri[3]={s.vertices[t[0]],s.vertices[t[1]],s.vertices[t[2]]};
  Bounds tb=empty();for(auto p:tri)include(tb,p);if(!overlap(tb,common,0))continue;Poly p=clip_above(tri,h);if(p.size()<3)continue;out.push_back({p,poly_bounds(p)});}return out;};
 const auto pa=clipped(a),pb=clipped(b);
 for(const auto&[x,xb]:pa)for(const auto&[y,yb]:pb)if(overlap(xb,yb,0)&&polys_cross(x,y))return true;
 return false;
}
bool water_blocked(const Solid&water,const Solid&other,float step){
 if(water.triangles.empty()||other.triangles.empty()||!valid(water.bounds)||!valid(other.bounds)||!std::isfinite(step))return false;
 // the surface raised by the step; only what reaches up through it counts
 Bounds lifted=water.bounds;lifted.minimum.y+=step;lifted.maximum.y+=step;if(!overlap(lifted,other.bounds,0))return false;
 std::vector<std::pair<Poly,Bounds>> others;
 for(const auto&t:other.triangles){Poly p{other.vertices[t[0]],other.vertices[t[1]],other.vertices[t[2]]};const Bounds b=poly_bounds(p);if(overlap(b,lifted,0))others.push_back({std::move(p),b});}
 for(const auto&t:water.triangles){Poly p{water.vertices[t[0]],water.vertices[t[1]],water.vertices[t[2]]};for(auto&q:p)q.y+=step;const Bounds b=poly_bounds(p);
  for(const auto&[o,ob]:others)if(overlap(b,ob,0)&&polys_cross(p,o))return true;}
 return false;
}
bool ground_under(const Solid&s,Point above,float&height){
 bool found=false;if(!valid(s.bounds)||above.x<s.bounds.minimum.x||above.x>s.bounds.maximum.x||above.z<s.bounds.minimum.z||above.z>s.bounds.maximum.z||above.y<s.bounds.minimum.y)return false;
 for(const auto&t:s.triangles){const Point a=s.vertices[t[0]],b=s.vertices[t[1]],c=s.vertices[t[2]];
  const double ux=double(b.x)-a.x,uy=double(b.y)-a.y,uz=double(b.z)-a.z,vx=double(c.x)-a.x,vy=double(c.y)-a.y,vz=double(c.z)-a.z;
  const double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,len=std::sqrt(nx*nx+ny*ny+nz*nz);
  if(!(len>0)||ny<kGroundFacing*len)continue;  // a wall, an overhang's underside, a ceiling
  // (x, z) inside the triangle seen from above (edges included)
  const double px=double(above.x)-a.x,pz=double(above.z)-a.z,d=ux*vz-uz*vx;if(d==0)continue;
  const double s1=(px*vz-pz*vx)/d,s2=(ux*pz-uz*px)/d;if(s1<-1e-9||s2<-1e-9||s1+s2>1+1e-9)continue;
  const double y=a.y+s1*uy+s2*vy;if(y>double(above.y)+1e-3)continue;
  if(!found||y>height)height=float(y),found=true;}
 return found;
}
// ---- an object's own triangles against what is kept clear (issue #281): check() and check_building() with a solid
namespace {
double axis_of(Point p,int k){return k==0?p.x:k==1?p.y:p.z;}
// the part of a convex polygon on one side of the plane `axis` = h (Sutherland-Hodgman)
Poly clip_axis(const Poly&in,int k,double h,bool above){
 Poly out;const size_t n=in.size();
 for(size_t j=0;j<n;++j){const Point&a=in[j],&b=in[(j+1)%n];const double va=axis_of(a,k),vb=axis_of(b,k);const bool ia=above?va>=h:va<=h,ib=above?vb>=h:vb<=h;
  if(ia)out.push_back(a);
  if(ia!=ib){const double f=(h-va)/(vb-va);Point c{float(a.x+(b.x-a.x)*f),float(a.y+(b.y-a.y)*f),float(a.z+(b.z-a.z)*f)};if(k==0)c.x=float(h);else if(k==1)c.y=float(h);else c.z=float(h);out.push_back(c);}}
 return out;
}
Poly triangle_of(const Solid&s,const std::array<uint16_t,3>&t){return {s.vertices[t[0]],s.vertices[t[1]],s.vertices[t[2]]};}
Bounds bounds_of(const Poly&p){Bounds b=empty();for(auto q:p)include(b,q);return b;}
struct V {double x,y,z;};
V vec(Point p){return {p.x,p.y,p.z};}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V add_scaled(V a,V d,double t){return {a.x+d.x*t,a.y+d.y*t,a.z+d.z*t};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
double length2(V a){return dot(a,a);}
// the squared distance of a point to a triangle (Ericson, Real-Time Collision Detection 5.1.5)
double point_triangle2(V p,V a,V b,V c){
 const V ab=sub(b,a),ac=sub(c,a),ap=sub(p,a);const double d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0&&d2<=0)return length2(ap);
 const V bp=sub(p,b);const double d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return length2(bp);
 const double vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return length2(sub(p,add_scaled(a,ab,d1/(d1-d3))));
 const V cp=sub(p,c);const double d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return length2(cp);
 const double vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return length2(sub(p,add_scaled(a,ac,d2/(d2-d6))));
 const double va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)return length2(sub(p,add_scaled(b,sub(c,b),(d4-d3)/((d4-d3)+(d5-d6)))));
 const double denom=1/(va+vb+vc);return length2(sub(p,add_scaled(add_scaled(a,ab,vb*denom),ac,vc*denom)));
}
// the squared distance between two segments (Ericson 5.1.9)
double segment_segment2(V p1,V q1,V p2,V q2){
 const V d1=sub(q1,p1),d2=sub(q2,p2),r=sub(p1,p2);const double a=length2(d1),e=length2(d2),f=dot(d2,r);double s=0,t=0;
 if(a<=1e-12&&e<=1e-12)return length2(r);
 if(a<=1e-12)t=std::clamp(f/e,0.0,1.0);
 else{const double c=dot(d1,r);if(e<=1e-12)s=std::clamp(-c/a,0.0,1.0);
  else{const double b=dot(d1,d2),den=a*e-b*b;s=den>1e-12?std::clamp((b*f-c*e)/den,0.0,1.0):0;t=(b*s+f)/e;if(t<0){t=0;s=std::clamp(-c/a,0.0,1.0);}else if(t>1){t=1;s=std::clamp((b-c)/a,0.0,1.0);}}}
 return length2(sub(add_scaled(p1,d1,s),add_scaled(p2,d2,t)));
}
// the distance in X/Z of a point to a polygon's outline, 0 inside it
double outline_distance(const Poly&p,double x,double z){
 bool inside=false;double best=1e300;const size_t n=p.size();
 for(size_t j=0,k=n-1;j<n;k=j++){const double xj=p[j].x,zj=p[j].z,xk=p[k].x,zk=p[k].z;
  if((zj>z)!=(zk>z)&&x<(xk-xj)*(z-zj)/(zk-zj)+xj)inside=!inside;
  const double ex=xk-xj,ez=zk-zj,l=ex*ex+ez*ez,t=l>0?std::clamp(((x-xj)*ex+(z-zj)*ez)/l,0.0,1.0):0;
  best=std::min(best,std::hypot(xj+ex*t-x,zj+ez*t-z));}
 return inside&&n>=3?0:best;
}
}
namespace {
bool solid_touches_box(const Near&n,const Bounds&box){
 return n.visit(box,[&](uint32_t j){Poly p=triangle_of(n.s,n.s.triangles[j]);
  for(int k=0;k<3&&!p.empty();++k){p=clip_axis(p,k,axis_of(box.minimum,k),true);if(!p.empty())p=clip_axis(p,k,axis_of(box.maximum,k),false);}
  return !p.empty();});
}
bool solid_in_column(const Near&n,Point c,double reach,double low,double high){
 const Bounds column{{float(c.x-reach),float(low),float(c.z-reach)},{float(c.x+reach),float(high),float(c.z+reach)}};
 return n.visit(column,[&](uint32_t j){Poly p=triangle_of(n.s,n.s.triangles[j]);
  p=clip_axis(p,1,low,true);if(!p.empty())p=clip_axis(p,1,high,false);
  return !p.empty()&&outline_distance(p,c.x,c.z)<=reach;});
}
bool solid_near_segment(const Near&n,Point a,Point b,double reach){
 Bounds corridor=empty();include(corridor,a);include(corridor,b);
 corridor.minimum={float(corridor.minimum.x-reach),float(corridor.minimum.y-reach),float(corridor.minimum.z-reach)};corridor.maximum={float(corridor.maximum.x+reach),float(corridor.maximum.y+reach),float(corridor.maximum.z+reach)};
 const double r2=reach*reach;const V p=vec(a),q=vec(b);
 return n.visit(corridor,[&](uint32_t j){const auto&t=n.s.triangles[j];const Point x=n.s.vertices[t[0]],y=n.s.vertices[t[1]],z=n.s.vertices[t[2]];
  if(segment_triangle(a,b,x,y,z))return true;
  const V vx=vec(x),vy=vec(y),vz=vec(z);
  return point_triangle2(p,vx,vy,vz)<=r2||point_triangle2(q,vx,vy,vz)<=r2||segment_segment2(p,q,vx,vy)<=r2||segment_segment2(p,q,vy,vz)<=r2||segment_segment2(p,q,vz,vx)<=r2;});
}
// the room's triangles near an object that its own triangles cross above the higher base plus kGroundStep (as
// solids_overlap: resting on the ground, or reaching a little into a slope under it, doesn't count); how many, the first
unsigned solid_crossings(const Near&near,const std::vector<const ProtectedBounds*>&walls,std::string&first){
 const Solid&s=near.s;if(s.triangles.empty()||walls.empty())return 0;
 float base=std::numeric_limits<float>::infinity();for(const auto*w:walls)base=std::min(base,w->bounds.minimum.y);
 const double h=double(std::max(s.bounds.minimum.y,base))+kGroundStep;
 // the object's triangles above h, clipped once when a wall first meets them
 std::vector<Poly> own(s.triangles.size());std::vector<uint8_t> made(s.triangles.size(),0);
 unsigned n=0;
 for(const auto*w:walls){if(w->bounds.maximum.y<h)continue;const Point tri[3]={w->corners[0],w->corners[1],w->corners[2]};const Poly p=clip_above(tri,h);if(p.size()<3)continue;const Bounds pb=bounds_of(p);
  if(near.visit(pb,[&](uint32_t j){if(!made[j]){const auto&t=s.triangles[j];const Point o[3]={s.vertices[t[0]],s.vertices[t[1]],s.vertices[t[2]]};own[j]=clip_above(o,h);made[j]=1;}
     return own[j].size()>=3&&overlap(pb,bounds_of(own[j]),0)&&polys_cross(p,own[j]);}))if(!n++)first=w->id;}
 return n;
}
}
}
