// SPDX-License-Identifier: MPL-2.0
// Kept in the central profile owner's translation unit: no second lookup owner.
namespace native_actor_profiles { namespace {
struct GuestEntry {
    Entry profile;
    std::string owner,fingerprint;
    std::array<uint8_t,kGuestSlotBytes> bytes{};
};
std::array<GuestEntry,kGuestCapacity> guest_entries{};
uint32_t guest_pool=0;
const Entry* guest_entry(uint32_t id){for(const auto&e:guest_entries)if(e.profile.address&&e.profile.id==id)return &e.profile;return nullptr;}
bool equal_name(uint32_t at,const uint8_t* name){for(unsigned i=0;i<8;++i){const uint8_t a=ld8(at+i);if(a!=name[i])return false;if(!a)return true;}return true;}
void search_guest_name(Cpu*c){for(const auto&e:guest_entries)if(e.profile.address&&equal_name(c->r[3],e.bytes.data()+0x44)){c->r[3]=e.profile.address+0x44;return;}original(c,GC(0x025C109C));}
void get_guest_name(Cpu*c){for(const auto&e:guest_entries)if(e.profile.address&&e.profile.id==c->r[3]){c->r[3]=e.profile.address+0x50;return;}original(c,GC(0x025C10E8));}
}
bool install_guest(const Allocate&allocate,const Owner&owner,const Replace&replace,std::string*why){
    auto refuse=[&](const char*text){if(why)*why=text;return false;};
    if(guest_enabled)return true;
    // The mapped retail table must end at the known final process. Never index
    // extension IDs through retail fpcPf_Get, which has no bounds check.
    const uint32_t last=ld32(GD(0x101F3EE0)+0x1E3*4);
    if(last<0x10000000||last>0x50000000-0x30||ld16(last+8)!=0x1E3)return refuse("unsupported native profile table");
    if(!available(owner,why))return false;
    for(uint32_t a:{GC(0x025C109C),GC(0x025C10E8)})if(!owner(a).empty())return refuse("native actor name lookup already owned");
    // Allocate last: a refused bridge leaves the startup allocator untouched.
    const uint32_t pool=allocate();
    if(!valid_storage(pool)||pool>mem::kHostStart-kGuestCapacity*kGuestSlotBytes)return refuse("invalid guest profile pool");
    guest_pool=pool;guest_enabled=true;
    if(!install(owner,replace,why)){guest_enabled=false;guest_pool=0;return false;}
    replace(GC(0x025C109C),search_guest_name);replace(GC(0x025C10E8),get_guest_name);
    return true;
}
int32_t register_guest(const std::string&owner,const std::string&fingerprint,uint32_t d,const OwnRange&range,const OwnFunction&function,std::string*why){
    auto refuse=[&](int32_t code,const char*text){if(why)*why=text;return code;};
    if(!guest_enabled)return refuse(-4,"guest actor profile bridge unavailable");
    if(guest_closed)return refuse(-3,"actor profile registration closed before first process allocation");
    if(owner.empty()||owner.size()>64||fingerprint.size()!=64||(d&3)||!range(d,28)||ld32(d)!=28||ld32(d+4)!=1)return refuse(-1,"invalid actor profile descriptor");
    const uint32_t source=ld32(d+8);
    if((source&3)||!range(source,0x30))return refuse(-1,"profile is not owned by the calling module");
    const uint16_t id=ld16(source+8);
    if(id<0x1E4||id>=kFirstReserved)return refuse(-1,"profile ID collides with retail or port reservation");
    if(guest_entry(id))return refuse(-2,"profile ID already registered");
    const uint32_t methods=ld32(source+0x24);
    if((methods&3)||!range(methods,20))return refuse(-1,"method table is not owned by the calling module");
    for(unsigned i=0;i<5;++i)if(!function(ld32(methods+4*i)))return refuse(-1,"profile callback is not a function owned by the calling module");
    const uint32_t size=ld32(source+0x10),extra=ld32(source+0x14);
    if(size<0x3AC||size>0x100000||extra>0x100000||size+extra>0x100000)return refuse(-1,"actor allocation size outside supported bounds");
    // Use the region's retail actor base methods; never admit module pointers
    // as native process/leaf method tables.
    const uint32_t native=ld32(GD(0x101F3EE0)+0xB6*4);
    if(native<0x10000000||native>0x50000000-0x30)return refuse(-4,"native actor template unavailable");
    for(unsigned offset:{0xCu,0x1Cu})if(ld32(source+offset)&&ld32(source+offset)!=ld32(native+offset))return refuse(-1,"foreign native base method pointer");
    const int32_t argument=int32_t(ld32(d+20));const uint32_t gba=ld32(d+24);
    if(argument< -1||argument>127||gba>255)return refuse(-1,"invalid actor name subtype");
    std::array<uint8_t,8> name{};bool ended=false;
    for(unsigned i=0;i<8;++i){const uint8_t ch=ld8(d+12+i);if(!ch)ended=true;else if(ended||!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'))return refuse(-1,"invalid actor name");name[i]=ch;}
    if(!name[0])return refuse(-1,"empty actor name");
    for(unsigned i=0;i<0x329;++i)if(equal_name(GD(0x101EBFDC)+12*i,name.data()))return refuse(-2,"actor name collides with retail");
    for(const auto&e:guest_entries)if(e.profile.address&&std::equal(name.begin(),name.end(),e.bytes.begin()+0x44))return refuse(-2,"actor name already registered");
    for(unsigned i=0;i<guest_entries.size();++i)if(!guest_entries[i].profile.address){
        GuestEntry e;e.owner=owner;e.fingerprint=fingerprint;e.profile={id,guest_pool+i*kGuestSlotBytes,nullptr};
        const uint32_t at=e.profile.address;
        std::memcpy(e.bytes.data(),mem::ptr(source),0x30);std::memcpy(e.bytes.data()+0x30,mem::ptr(methods),20);
        std::copy(name.begin(),name.end(),e.bytes.begin()+0x44);std::copy(name.begin(),name.end(),e.bytes.begin()+0x50);
        std::memcpy(mem::ptr(at),e.bytes.data(),e.bytes.size());
        for(unsigned offset:{0xCu,0x1Cu})st32(at+offset,ld32(native+offset));
        st16(at+0xA,0);st16(at+0x22,0);st16(at+0x2E,0);
        st32(at+0x24,at+0x30);st16(at+0x4C,id);st8(at+0x4E,uint8_t(argument));st8(at+0x4F,uint8_t(gba));
        std::memcpy(e.bytes.data(),mem::ptr(at),e.bytes.size());guest_entries[i]=std::move(e);return id;
    }
    return refuse(-5,"guest actor profile capacity exhausted");
}
std::string guest_identity(){
    std::string out;std::array<const GuestEntry*,kGuestCapacity> sorted{};unsigned n=0;
    for(const auto&e:guest_entries)if(e.profile.address)sorted[n++]=&e;
    std::sort(sorted.begin(),sorted.begin()+n,[](const auto*a,const auto*b){return a->profile.id<b->profile.id;});
    for(unsigned i=0;i<n;++i){const auto&e=*sorted[i];out.push_back(char(e.owner.size()));out+=e.owner;out+=e.fingerprint;out.append(reinterpret_cast<const char*>(e.bytes.data()),e.bytes.size());}
    return out;
}
void restore_guest_snapshots(){for(const auto&e:guest_entries)if(e.profile.address)std::memcpy(mem::ptr(e.profile.address),e.bytes.data(),e.bytes.size());}
}
