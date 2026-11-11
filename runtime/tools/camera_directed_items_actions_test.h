// Accepted initial-facing contracts, not native shot/hit or pitch-handoff acceptance.
// Included by camera_directed_items_test.cpp to share its mapped guest/camera fixture.
#include <cstdio>
extern "C" {
void site_023E7CF4(Cpu*); void site_023E88F4(Cpu*);
void site_023E8D34(Cpu*); void site_023E8E40(Cpu*);
void site_023E8A58(Cpu*); void site_023E8B20(Cpu*);
void site_023E902C(Cpu*); void site_023E9130(Cpu*);
void site_023E9E98(Cpu*); void site_023EDFC4(Cpu*); void site_0243C830(Cpu*);
void site_023EDE00(Cpu*);
void wwhd_item_facing(Cpu*, unsigned, unsigned, unsigned, unsigned);
}
namespace {
// extra: the register of the value the handler checks besides Link (the bomb's throw type, the bottle's ship use)
struct ActionSite { void (*call)(Cpu*); unsigned reg; uint32_t proc; uint16_t item; uint32_t site; int extra; };
constexpr ActionSite actions[] = {
    {site_023E7CF4,31,0x94,0x27,0x023E7CF4,-1}, {site_023E88F4,31,0x95,0x27,0x023E88F4,-1},
    {site_023E8D34,31,0x80,0x2D,0x023E8D34,-1}, {site_023E8E40,31,0x81,0x2D,0x023E8E40,-1},
    {site_023E8A58,31,0x83,0x2F,0x023E8A58,-1}, {site_023E8B20,31,0x84,0x2F,0x023E8B20,-1},
    {site_023E902C,31,0x76,0x25,0x023E902C,-1}, {site_023E9130,31,0x7D,0x25,0x023E9130,-1},
    {site_023E9E98,30,0x71,0x100,0x023E9E98,31},
    {site_023EDFC4,31,0x51,0x33,0x023EDFC4,-1}, {site_0243C830,31,0x53,0x33,0x0243C830,-1},
    {site_023EDE00,30,0xA5,0x50,0x023EDE00,29},
};
Cpu action_fixture(const ActionSite& a) {
    room_fixture();st32(player_addr+0x65F0,a.proc);st16(player_addr+0x69B0,a.item);
    Cpu c;std::memset(&c,0xA5,sizeof(c));c.r[29]=c.r[30]=c.r[31]=0;c.r[a.reg]=player_addr;
    if(a.item==0x100) {
        st16(player_addr+0x69B2,0x31);st32(player_addr+0x65A4,player_addr+0x9000);st32(player_addr+0x65A0,1234);
        st32(player_addr+0x9004,1234);st16(player_addr+0x9008,0x126);
    }
    return c;
}
void check_action(const ActionSite& a,Cpu& c,bool facing,uint16_t yaw=0x4000) {
    Cpu expected;std::memcpy(&expected,&c,sizeof(c));
    unsigned char actor[65536];std::memcpy(actor,mem::ptr(player_addr),sizeof(actor));
    if(facing){actor[0x322]=actor[0x32A]=yaw>>8;actor[0x323]=actor[0x32B]=yaw&255;}
    // the decomp build's port point first (the decompilation's port_hooks.h PORT_ITEM_FACING: Link and the extra value
    // as the decompiled source passes them): the same writes as the hook, the Cpu unchanged; then the hook itself
    {
        static unsigned char before[65536];std::memcpy(before,mem::ptr(player_addr),sizeof(before));
        Cpu p;std::memcpy(&p,&c,sizeof(c));
        wwhd_item_facing(&p,c.r[1],a.site,c.r[a.reg],a.extra>=0?c.r[a.extra]:0);
        assert(!std::memcmp(&p,&c,sizeof(c)));
        assert(!std::memcmp(actor,mem::ptr(player_addr),sizeof(actor)));
        std::memcpy(mem::ptr(player_addr),before,sizeof(before));
    }
    a.call(&c);
    assert(!std::memcmp(&c,&expected,sizeof(c)));
    if(std::memcmp(actor,mem::ptr(player_addr),sizeof(actor)))
        std::fprintf(stderr,"initial-facing proc=%X item=%X expected=%d camera-mode=%X demo=%X\n",
            unsigned(a.proc),unsigned(ld16(player_addr+0x69B0)),int(facing),unsigned(ld32(GD(0x1046F0B0)+0x5B00)),unsigned(ld32(player_addr+0x430)));
    assert(!std::memcmp(actor,mem::ptr(player_addr),sizeof(actor)));
}
bool matching_item(const ActionSite& a,unsigned item) {
    if(a.proc==0x94||a.proc==0x95)return item==0x27||item==0x35||item==0x36;
    return item==a.item;
}
void action_tests() {
    for(const auto& a:actions) {
        auto c=action_fixture(a);mods::set_camera_directed_items(false);check_action(a,c,false);
        mods::set_camera_directed_items(true);c=action_fixture(a);check_action(a,c,true);
        // Test the actual exported callback with live caller registers, not a private matcher.
        for(unsigned gate=0;gate<22;++gate) {
            c=action_fixture(a);
            switch(gate) {
            case 0:focus=false;break;case 1:blocked=true;break;case 2:blocked=true;break;  // (gate 2 was the retired True 60 preview)
            case 3:st32(player_addr+0x834,0);break;case 4:st32(player_addr+0x6A70,0x80);break;
            case 5:st32(player_addr+0x65B0,process);break;
            case 6:st32(GD(0x1046F0B0)+0x5B34,player_addr+4);break;
            case 7:st32(GD(0x1046F0B0)+0x52E4,1);break;
            case 8:st32(camera+0x510,0x20000020);break;
            case 9:st32(camera+0x51C,53);st32(camera+0x520,53);st32(GD(0x1046F0B0)+0x5B00,0x10);break;
            case 10:st32(camera+0x520,7);break;
            case 11:st32(camera+0x140,0);break;
            case 12:st32(camera+0x514,57);break;
            case 13:st32(camera+0x12C,player_addr+4);break;
            case 14:st8(camera+0x518,1);break;
            case 15:st32(camera+0x528,60);break;
            case 16:stf32(camera+0x14,NAN);break;
            case 17:st32(GD(0x1046F0B0)+0x5AF8,0);break;
            case 18:c.r[3]=player_addr;c.r[a.reg]=0;break;
            case 19:c.r[3]=player_addr;c.r[a.reg]=player_addr+1;break;
            case 20:c.r[a.reg]=0xFFFF0000;break;
            case 21:st32(GD(0x1046F0B0)+0x5B00,0x10);break;
            }
            // Real native free-camera entries (including hammer) already carry slot bit10;
            // it is not exclusive SUBJECT ownership. Actual SUBJECT metadata still refuses.
            check_action(a,c,gate==21);
        }
        for(unsigned item=0;item<=0x100;++item) {
            c=action_fixture(a);st16(player_addr+0x69B0,item);check_action(a,c,matching_item(a,item));
        }
        c=action_fixture(a);st16(player_addr+0x69B0,0xFFFF);check_action(a,c,false);
        for(unsigned proc=0;proc<256;++proc) {
            c=action_fixture(a);st32(player_addr+0x65F0,proc);check_action(a,c,proc==a.proc);
        }
        for(unsigned heading=0;heading<4;++heading) {
            c=action_fixture(a);stf32(camera+0x10,heading==1?100:heading==3?-100:0);
            stf32(camera+0x18,heading==0?100:heading==2?-100:0);
            check_action(a,c,true,uint16_t(heading*0x4000));
        }
        if(a.item==0x100) {
            for(unsigned gate=0;gate<7;++gate) {
                c=action_fixture(a);
                switch(gate) {
                case 0:c.r[31]=1;break;case 1:st16(player_addr+0x69B2,0x2D);break;
                case 2:st32(player_addr+0x65A4,0);break;case 3:st32(player_addr+0x65A4,player_addr+0x9001);break;
                case 4:st32(player_addr+0x65A0,1235);break;case 5:st16(player_addr+0x9008,0x127);break;
                case 6:st32(player_addr+0x65A0,0xFFFFFFFF);st32(player_addr+0x9004,0xFFFFFFFF);break;
                }
                check_action(a,c,false);
            }
        }
        if(a.item==0x50) {
            c=action_fixture(a);c.r[29]=1;check_action(a,c,false);
        }
    }
    // Leaving aim/action or replacing the snapshot has no queued host heading to replay.
    fixture();mods::set_camera_directed_items(true);
}
}
