// SPDX-License-Identifier: MPL-2.0
// Synthetic bounded native graph data reused from the genuine graph parser contract.
namespace ModelG=story_native_people_model_graph;
static void graph_put(auto&b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
struct SavedModelFixture {
 Cpu cpu{};bool quiescent=true,quiescence_current=true,invalidate_quiescence_on_read=false,wrong_cpu=false;std::shared_ptr<threads::NativeQuiescence> quiescence;
 ModelG::Expected e;std::map<uint32_t,std::vector<uint8_t>>memory;bool preclaim=true,pinned=true,dispatch=true,dispatch_current=true;std::shared_ptr<story_native_refresh_dispatch::Guard> guard;unsigned reads=0;bool unstable=false,invalidate_on_read=false;
 uint32_t control=0x45000000,info=0x20100000,ids=0x20100100,resource=0x49000000,data=0x49000200,rows=0x49000300,fmdl=0x4A000400,skeleton=0x4A000480,bones=0x4A001000,raw=0x4A000000;
 std::array<uint32_t,2>actor{0x20001000,0x20002000},heap{0x48000000,0x48010000},morf{},model{},ref{},anim{0x49000500,0x49000600};
 SavedModelFixture(){quiescence=std::make_shared<threads::NativeQuiescence>(threads::NativeQuiescenceIssuer::make([this](const Cpu*c){return c==&cpu&&quiescence_current;}));guard=std::make_shared<story_native_refresh_dispatch::Guard>(std::move(*guestmods::acquire_native_people_refresh_dispatch()));e.identity={0x20010000,0x60000000,0x520,1,2,3,4};
  auto region=[&](uint32_t p,unsigned bytes){memory[p].resize(bytes);};
  region(GD(0x101C41D4),0x28);w(GD(0x101C41D4)+0x1C,0x432A0000);w(GD(0x101C41D4)+0x24,0x43160000);
  region(control,0x2130);region(info,0x8C);region(ids,48);region(resource,0x10000);region(raw,0x10000);
  region(GD(0x1018C7B0),4);w(GD(0x1018C7B0),GD(0x101FF5F0));region(GD(0x101FF5F0),0x30);
  region(GD(0x101F4F28),4);w(GD(0x101F4F28),control);region(GD(0x10058CC0),40);unsigned n=0;for(uint32_t v:{0u,0u,0u,GC(0x025EE1D8),0u,GC(0x027F3B28),0u,GC(0x028F036C),0u,GC(0x025E5C38)})w(GD(0x10058CC0)+4*n++,v);
  region(GD(0x1016C6C0),32);n=0;for(uint32_t v:{0xFFFFu,GC(0x027E4D20),0xFFFFu,GC(0x027E50A8),0xFFFFu,GC(0x027E565C),0xFFFFu,GC(0x027E5DA4)})w(GD(0x1016C6C0)+4*n++,v);
  region(GD(0x10145B38),0x70);w(GD(0x10145B38)+0x5C,GC(0x02755280));w(GD(0x10145B38)+0x6C,GC(0x02755298));
  auto init_heap=[&](uint32_t p){w(p+0xC,GD(0x10145B38));w(p+0x1C,p);w(p+0x20,0x10000);};init_heap(resource);
  w(control,GD(0x100E1A5C));w(control+0x14,control+0x28);w(control+0x1C,control+0x28);w(control+0x20,1);w(control+0x24,128);w(control+0x28+0x10,GD(0x100E1A6C));w(control+0x28+0x18,info);w(control+0x28+0x1C,control+0x14);
  w(info+0x10,info+0x1C);w(info+0x14,GD(0x100E19FC));w(info+0x18,64);w(info+0x1C,0x556F0000);w(info+0x5C,0x20100200);w(info+0x64,ids);w(info+0x80,resource);w(info+0x88,12);w(ids+40,data);w(ids+12,anim[0]);w(ids+16,anim[1]);
  region(GD(0x1016D820),32);n=0;for(uint32_t v:{0u,0u,0u,GC(0x028F036C),0u,GC(0x028F036C),0u,GC(0x027E0288)})w(GD(0x1016D820)+4*n++,v);
  region(GD(0x1016C6E0),64);n=0;for(uint32_t v:{0xFFFFu,GC(0x027E7164),0xFFFFu,GC(0x027E72CC),0xFFFFu,GC(0x027E7538),0xFFFFu,GC(0x027E7A14),0xFFFFu,GC(0x027E89FC),0xFFFFu,GC(0x027E8C4C),0xFFFFu,GC(0x027E8E54),0xFFFFu,GC(0x027E9020)})w(GD(0x1016C6E0)+4*n++,v);
  region(GD(0x1016C720),24);n=0;for(uint32_t v:{0xFFFFu,GC(0x027E92A8),0xFFFFu,GC(0x027E9378),0xFFFFu,GC(0x027E94D4)})w(GD(0x1016C720)+4*n++,v);
  region(GD(0x1016C738),168);n=0;for(uint32_t v:{0xFFFFu,GC(0x027E95E0),0xFFFFu,GC(0x027E96B0),0xFFFFu,GC(0x027E9780),0xFFFFu,GC(0x027E9850),0xFFFFu,GC(0x027E98F8),0xFFFFu,GC(0x027E99A0),0xFFFFu,GC(0x027E9A48),0xFFFFu,GC(0x027E9AE0),0xFFFFu,GC(0x027E9B90),0xFFFFu,GC(0x027E924C),0xFFFFu,GC(0x027E91F0),0xFFFFu,GC(0x027E9194),0xFFFFu,GC(0x027E9C38),0xFFFFu,GC(0x027E9C88),0xFFFFu,GC(0x027E9CD8),0xFFFFu,GC(0x027E1CF0),0xFFFFu,GC(0x027E1CF0),0xFFFFu,GC(0x027E1CF0),0xFFFFu,GC(0x027E1E9C),0xFFFFu,GC(0x027E1E9C),0xFFFFu,GC(0x027E1E9C)})w(GD(0x1016C738)+4*n++,v);
  w(data,fmdl);w(data+4,3);w(data+8,rows);w(fmdl+0xC,skeleton-fmdl-0xC);w(skeleton+8,3u<<16);w(skeleton+0x14,bones-skeleton-0x14);
  for(unsigned j=0;j<3;++j){w(rows+j*0x1C,bones+j*0x40);w(rows+j*0x1C+8,j?GC(0x022BE034):0);w(bones+j*0x40+4,j<<16);}
  w(rows+0x14,0xDEAD0000); // old retail shared binding: replaced before native use
  for(unsigned j=0;j<2;++j){region(actor[j],0x920);region(heap[j],0x10000);init_heap(heap[j]);morf[j]=heap[j]+0x100;model[j]=heap[j]+0x200;ref[j]=heap[j]+0x400;auto&a=e.actors[j];a.address=actor[j];a.pid=10+j;a.token=1+j;a.profile=0x175;a.subtype=1;a.fingerprint.fill(j+1);
   w(actor[j]+4,a.pid);w(actor[j]+0xC,0x00000175);w(actor[j]+0xF4,heap[j]);w(actor[j]+0x2DC,0x00010000);w(actor[j]+0x3B4,0x01020000);w(actor[j]+0x44C,morf[j]);w(actor[j]+0x8A0,GD(0x101C41D4));w(actor[j]+0x904,0x00010000);
   w(morf[j],GD(0x10058CC0));w(morf[j]+4,morf[j]+8);w(morf[j]+0x18,GC(0x027DAAA0));w(morf[j]+0x30,heap[j]+0x1000);w(morf[j]+0x5C,0xFFFF);w(morf[j]+0x64,0xFFFF);w(morf[j]+0x60,GC(0x027E6EA8));w(morf[j]+0x68,GC(0x027E7004));w(morf[j]+0x78,heap[j]+0x2000);w(morf[j]+0x7C,3);w(morf[j]+0x90,model[j]);w(morf[j]+0x94,anim[j]);w(morf[j]+0xA8,heap[j]+0x3000);w(morf[j]+0xAC,heap[j]+0x4000);
   w(model[j]+0x2C,ref[j]);w(model[j]+0xAC,data);w(model[j]+0xB8,actor[j]);w(ref[j],skeleton);w(ref[j]+8,bones);w(ref[j]+0xC,heap[j]+0x5000);w(ref[j]+0x10,heap[j]+0x6000);w(ref[j]+0x2C,3u<<16);w(ref[j]+0x30,0xDEAD0000);w(ref[j]+0x38,heap[j]+0x7000);w(anim[j]+4,GD(0x1016E95C));w(anim[j]+8,raw+0x800+j*0x100);
   const auto ad=raw+0x800+j*0x100,tracks=raw+0x1800+j*0x100,binding=raw+0x1A00+j*0x100;
   w(ad+0x14,3u<<16);w(ad+0x18,3);w(ad+0x20,tracks-ad-0x20);w(ad+0x24,skeleton-ad-0x24);w(ad+0x28,binding-ad-0x28);
   w(morf[j]+0x10,0x41F00000);w(morf[j]+0x20,heap[j]+0x4800);w(morf[j]+0x24,3);w(morf[j]+0x28,3);w(morf[j]+0x38,GD(0x1016D820));w(morf[j]+0x3C,heap[j]+0x4500);w(morf[j]+0x40,3);w(morf[j]+0x44,0x00030003);w(morf[j]+0x48,ad);w(morf[j]+0x4C,tracks);w(morf[j]+0x6C,skeleton);w(morf[j]+0x9C,0x41200000);
   for(unsigned k=0;k<3;++k){w(binding+k*2,(k<<16)|((k+1)%3));w(heap[j]+0x4500+k*4,k);w(heap[j]+0x4800+k*12+4,0x41F00000);const auto row=tracks+k*0x18,curve=raw+0x2000+j*0x1000+k*0x100,keys=raw+0x2400+j*0x1000+k*0x100,values=raw+0x2800+j*0x1000+k*0x100;
    w(row+8,0x00000100);w(row+0x10,curve-row-0x10);w(curve,2);w(curve+0xC,0x41F00000);w(curve+0x10,0x3F800000);w(curve+0x1C,keys-curve-0x1C);w(curve+0x20,values-curve-0x20);w(keys+4,0x41F00000);w(values,0x3F800000);w(values+16,0x3F800000);
   }

  }
  seed_cache();
 }
 void seed_cache(){const uint32_t loader=0x21000000,holder=0x21100000,res=0x4A009000,payload=raw;
  memory[GD(0x1015E184)].resize(0x40);w(GD(0x1015E184)+0x3C,GC(0x0275E208));w(GD(0x1015E184)+0x34,GC(0x0275CF28));
  memory[GD(0x101F4F7C)].resize(4);memory[loader].resize(0x1131C);memory[holder].resize(0x54);memory[res].resize(0x44);memory[payload].resize(0x10000);memory[0x20100200].resize(0x24);
  w(GD(0x101F4F7C),loader);w(loader,GD(0x100E222C));w(loader+0x14,loader+0x28);w(loader+0x1C,loader+0x28);w(loader+0x20,1);w(loader+0x24,640);
  const auto node=loader+0x28;w(node+0xC,node+0x20);w(node+0x10,GD(0x100E20A4));w(node+0x14,GD(0x100E231C));w(node+0x18,holder);w(node+0x1C,loader+0x14);w(node+0x20,0x556F0000);
  w(holder+0xC,GD(0x100E25D0));w(holder+0x10,holder+0x1C);w(holder+0x14,GD(0x100E2254));w(holder+0x18,0x30);w(holder+0x1C,0x556F0000);w(holder+0x4C,res);w(holder+0x50,payload);w(info+0x7C,payload);
  w(payload+0xC,GD(0x10145B38));w(payload+0x1C,payload);w(payload+0x20,0x10000);w(res+0x10,GD(0x1015E184));const auto base=payload+0x40,fres=payload+0x100;w(res+0x14,base);w(res+0x18,0x8000);w(res+0x1C,0x8000);w(res+0x28,base);w(res+0x2C,base+0x14);w(res+0x30,base+0x38);w(res+0x34,1);w(res+0x38,base+0x20);w(res+0x3C,base+0xC0);w(0x20100200+0x14,fres);
  w(base,0x53415243);w(base+4,0x0014FEFF);w(base+8,0x8000);w(base+0xC,0xC0);w(base+0x10,0x01000000);w(base+0x14,0x53464154);w(base+0x18,0x000C0001);w(base+0x1C,0x65);uint32_t hash=0;for(unsigned char ch:std::string_view("Uo.bfres"))hash=hash*0x65+ch;w(base+0x20,hash);w(base+0x24,0x01000000);w(base+0x28,0);w(base+0x2C,0x7F40);w(base+0x30,0x53464E54);w(base+0x34,0x00080000);w(base+0x38,0x556F2E62);w(base+0x3C,0x66726573);w(base+0x40,0);
  const auto models=payload+0x200,animations=payload+0x280;w(fres,0x46524553);w(fres+0x20,models-fres-0x20);w(fres+0x28,animations-fres-0x28);w(fres+0x50,0x00010000);w(fres+0x54,0x00020000);w(models+0x24,fmdl-models-0x24);w(animations+0x24,raw+0x800-animations-0x24);w(animations+0x34,raw+0x900-animations-0x34);
 }
 void w(uint32_t p,uint32_t value){for(auto&[a,b]:memory)if(p>=a&&uint64_t(p)+4<=uint64_t(a)+b.size()){graph_put(b,p-a,value);return;}assert(false);}
 bool read(uint32_t p,std::span<uint8_t>out){++reads;if(invalidate_quiescence_on_read)quiescence_current=false;if(invalidate_on_read)dispatch_current=false;for(auto&[a,b]:memory)if(p>=a&&uint64_t(p)+out.size()<=uint64_t(a)+b.size()){std::copy_n(b.begin()+p-a,out.size(),out.begin());if(unstable&&p==morf[0]&&out.size()==0xC8)out[0x90]^=reads&1;return true;}return false;}
 ModelG::Sources source(){return {[&](auto p,auto b){return read(p,b);},[&](const auto&a,const auto&i){if(i!=e.identity)return false;for(const auto&x:e.actors)if(x==a)return true;return false;},[&](ModelG::Scope&s){s.expected=e;s.fenced=true;s.stage_live=true;s.resources_pinned=pinned;s.preclaim=preclaim;s.native_dispatch=dispatch?guard:nullptr;s.native_quiescence=quiescent?quiescence:nullptr;s.native_cpu=wrong_cpu?nullptr:&cpu;return true;}};}
};
