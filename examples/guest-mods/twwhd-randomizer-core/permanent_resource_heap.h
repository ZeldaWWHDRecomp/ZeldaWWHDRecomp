/* Complete public CC0 a874e1a9 HdRootTask::prepare (0203EE2C).
 * MIT misc_rando_features.asm adds 320KiB to the PermanentResource/dataheap.
 * No other heap budget, callback or initialization order is changed. */
#pragma once
#ifndef R_PERM_HEAP
#define R_PERM_HEAP(t,a) (*(volatile t*)(a))
#endif
#ifndef PERM_HEAP_DATA
#define PERM_HEAP_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x02035E2C,void,perm_hdRootProcessInitialize,(void));
WWHD_GAME_FUNC(0x02032B48,void,perm_hdRootSystemInitialize,(void));
WWHD_GAME_FUNC(0x02035D78,u32,perm_hdRootGetProcess,(void));
WWHD_GAME_FUNC(0x02035ED4,void,perm_hdRootSetProcessPhase,(u32 a0,u32 a1));
/* Canonical calls use import thunks, folded by Recompiler.import_calls into
 * these verified USA import slots. Thunk addresses are not dispatch entries. */
WWHD_GAME_FUNC(0xC00083D8,void,perm_WPADEnableURCC,(u32 a0));
WWHD_GAME_FUNC(0xC00083E8,void,perm_WPADEnableWiiRemote,(u32 a0));
WWHD_GAME_FUNC(0x026177B8,void,perm_hdRootInputInitialize,(u32 a0));
WWHD_GAME_FUNC(0x0273EFF8,u32,perm_hdRootLoadArchive,(u32 a0,u32 a1,u32 a2,u32 a3));
WWHD_GAME_FUNC(0x0273F9E0,u32,perm_hdRootCreateMultiArchive,(u32 a0,u32 a1,u32 a2,u32 a3));
WWHD_GAME_FUNC(0x0273FBB8,void,perm_hdRootAppendArchive,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x0274468C,u32,perm_hdRootCreateMount,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x02741770,void,perm_hdRootMountDevice,(u32 a0,u32 a1,u32 a2));
WWHD_GAME_FUNC(0xC00046E8,void,perm_nn_act_Initialize,(void));
WWHD_GAME_FUNC(0xC0004660,u32,perm_nn_act_GetSlotNo,(void));
WWHD_GAME_FUNC(0x027507E8,void,perm_hdRootHeapInitialize,(u32 a0));
WWHD_GAME_FUNC(0x02750940,void,perm_hdRootSelectDefaultHeap,(u32 a0,u32 a1,u32 a2));
WWHD_GAME_FUNC(0x02748974,u32,perm_hdRootGetTaskInfo,(u32 a0));
WWHD_GAME_FUNC(0x02617858,void,perm_hdRootInputConfigure,(u32 a0,u32 a1,u32 a2));
WWHD_GAME_FUNC(0x0203E8F4,u32,perm_hdRootGetMainHeap,(void));
WWHD_GAME_FUNC(0x02753004,u32,perm_hdRootCreateExpHeap,(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4));
WWHD_GAME_FUNC(0x0260FBF4,void,perm_hdRootResourceInitialize,(u32 a0));
WWHD_GAME_FUNC(0x0260FC98,void,perm_hdRootResourceSetHeap,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x0275320C,u32,perm_hdRootHeapOverhead,(u32 a0));
WWHD_GAME_FUNC(0x02603680,void,perm_hdRootModelInitialize,(u32 a0));
WWHD_GAME_FUNC(0x0260ABF8,void,perm_hdRootSaveInitialize,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x0203E8DC,u32,perm_hdRootGetArchiveHeap,(void));
WWHD_GAME_FUNC(0x02612E64,void,perm_hdRootLoadArchiveResource,(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,u32 a5,u32 a6));
WWHD_GAME_FUNC(0x026124B0,u32,perm_hdRootGetArchiveResource,(u32 a0,u32 a1,u32 a2,u32 a3));
WWHD_GAME_FUNC(0x027E2DC0,u32,perm_hdRootCastArchive,(u32 a0));
WWHD_GAME_FUNC(0x0203E8E8,u32,perm_hdRootGetGraphicsHeap,(void));
WWHD_GAME_FUNC(0x02005B90,void,perm_hdRootUiGraphicsInitialize,(u32 a0));
WWHD_GAME_FUNC(0x027DFA24,u32,perm_hdRootFindArchiveFile,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x02005C38,void,perm_hdRootSetShaderRanges,(u32 a0,u32 a1,u32 a2,u32 a3));
WWHD_GAME_FUNC(0x026FB344,void,perm_hdRootUiResourceInitialize,(u32 a0));
WWHD_GAME_FUNC(0x026FB3F0,void,perm_hdRootUiResourceSetHeap,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x0203E8B8,u32,perm_hdRootGetFontHeap,(void));
WWHD_GAME_FUNC(0x02720A5C,void,perm_hdRootFontInitialize,(u32 a0));
WWHD_GAME_FUNC(0x02720B34,void,perm_hdRootFontSetHeap,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x02754B38,u32,perm_hdRootCreateFrameHeap,(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4));
WWHD_GAME_FUNC(0x02720400,u32,perm_hdRootCreateFontResource,(u32 a0));
WWHD_GAME_FUNC(0x02720510,void,perm_hdRootRegisterFontResource,(u32 a0));
WWHD_GAME_FUNC(0x020360FC,void,perm_hdRootMiiverseInitialize,(u32 a0));
WWHD_GAME_FUNC(0x020361E4,void,perm_hdRootMiiverseConfigure,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x02748CFC,void,perm_hdRootTaskArgsConstruct,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x0203E8AC,u32,perm_hdRootGetChildHeap,(void));
WWHD_GAME_FUNC(0x02749ECC,u32,perm_hdRootCreateTask,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x027488E8,void,perm_hdRootBasePrepare,(u32 a0));
WWHD_GAME_FUNC(0x02748B98,void,perm_hdRootTaskArgsConstructAlternate,(u32 a0,u32 a1));
WWHD_GAME_FUNC(0x0203EA88,void,perm_graphics_heaps,(void));
#ifndef PERM_HEAP_CALL2
#define PERM_HEAP_CALL2(target,a,b) (((u32(*)(u32,u32))(target))(a,b))
#endif
#ifndef PERM_HEAP_CALL3
#define PERM_HEAP_CALL3(target,a,b,c) (((u32(*)(u32,u32,u32))(target))(a,b,c))
#endif
#ifndef PERM_HEAP_CALL1
#define PERM_HEAP_CALL1(target,a) (((u32(*)(u32))(target))(a))
#endif
static void perm_name(u32 at,u32 text){R_PERM_HEAP(u32,at+4)=PERM_HEAP_DATA(0x100063F8);R_PERM_HEAP(u32,at)=text;}
static u32 perm_data_offset(u32 word){const u32 offset=R_PERM_HEAP(u32,word);return offset?word+offset:0;}
static u32 perm_create_task(u32 self,u32 source,u32 destination,u32 callback){
    for(u32 i=0;i<32;i++)R_PERM_HEAP(u32,destination+i*4)=R_PERM_HEAP(u32,source+i*4);
    R_PERM_HEAP(u32,destination+0x7C)=callback;
    return perm_hdRootCreateTask(R_PERM_HEAP(u32,self+0x5C),destination);
}
static void perm_cast_task(u32 task,u32 guard,u32 type,u32 value){
    if(!R_PERM_HEAP(u32,guard)){R_PERM_HEAP(u32,guard)=1;R_PERM_HEAP(u32,type)=value;}
    if(task){const u32 table=R_PERM_HEAP(u32,task+0x70);(void)PERM_HEAP_CALL2(R_PERM_HEAP(u32,table+12),task,type);}
}
WWHD_REPLACE(0x0203EE2C,void,randomizer_permanent_resource_prepare,(u32 self)){
    /* Keep the native local views' overlap and alignment, without relying on
     * caller frame offsets or SDK caller-address tricks. */
    u32 storage[0x220/4] __attribute__((aligned(16)));
    const u32 frame=(u32)storage,config=PERM_HEAP_DATA(0x104612BC);
    u32 flags;
    if(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x104612B8)))flags=R_PERM_HEAP(u32,config+0x1C);
    else{
        R_PERM_HEAP(u32,PERM_HEAP_DATA(0x104612B8))=1;
        R_PERM_HEAP(u32,config+0x14)=1;R_PERM_HEAP(u8,config+0x20)=1;
        R_PERM_HEAP(u32,config)=PERM_HEAP_DATA(0x100064B8);
        R_PERM_HEAP(u32,config+0x10)=0x2332;R_PERM_HEAP(u32,config+0x18)=0x4D2;flags=0x140D;
    }
    R_PERM_HEAP(u32,config+8)=0x40000;R_PERM_HEAP(u32,config+12)=0x80000;R_PERM_HEAP(u32,config+4)=0x200000;
    R_PERM_HEAP(u32,config+0x1C)=(flags&0xFFFFFFEEu)|0x20;
    perm_hdRootProcessInitialize();perm_hdRootSystemInitialize();
    u32 process=perm_hdRootGetProcess();perm_hdRootSetProcessPhase(process,0);
    R_PERM_HEAP(u32,frame+0x70)=config;R_PERM_HEAP(u32,frame+0x78)=0;R_PERM_HEAP(u32,frame+0x7C)=0;
    perm_WPADEnableURCC(1);perm_WPADEnableWiiRemote(0);
    const u32 manager=R_PERM_HEAP(u32,self+0x5C),create=R_PERM_HEAP(u32,manager+0x40),table=R_PERM_HEAP(u32,create+0x24);
    (void)PERM_HEAP_CALL3(R_PERM_HEAP(u32,table+44),create,self,frame+0x70);
    perm_WPADEnableURCC(1);perm_WPADEnableWiiRemote(0);perm_hdRootInputInitialize(0);
    const u32 device=R_PERM_HEAP(u32,R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F8B08))+0x1C);
    const u32 name_a=frame+8,name_b=frame+0x10;
    perm_name(name_a,PERM_HEAP_DATA(0x10006624));perm_name(name_b,PERM_HEAP_DATA(0x1000659C));
    R_PERM_HEAP(u32,self+0x124)=perm_hdRootLoadArchive(0,name_b,device,name_a);
    perm_name(name_a,PERM_HEAP_DATA(0x10006630));perm_name(name_b,PERM_HEAP_DATA(0x1000663C));
    R_PERM_HEAP(u32,self+0x128)=perm_hdRootLoadArchive(0,name_a,device,name_b);
    perm_name(name_a,PERM_HEAP_DATA(0x100065A8));perm_name(name_b,PERM_HEAP_DATA(0x10006590));
    R_PERM_HEAP(u32,self+0x120)=perm_hdRootLoadArchive(0,name_a,device,name_b);
    perm_name(name_a,PERM_HEAP_DATA(0x10006598));perm_name(name_b,PERM_HEAP_DATA(0x100065A4));
    R_PERM_HEAP(u32,self+0x11C)=perm_hdRootLoadArchive(0,name_a,device,name_b);
    const u32 multi=perm_hdRootCreateMultiArchive(0,4,0,4);R_PERM_HEAP(u32,self+0x12C)=multi;
    perm_hdRootAppendArchive(multi,R_PERM_HEAP(u32,self+0x124));
    perm_hdRootAppendArchive(R_PERM_HEAP(u32,self+0x12C),R_PERM_HEAP(u32,self+0x128));
    perm_hdRootAppendArchive(R_PERM_HEAP(u32,self+0x12C),R_PERM_HEAP(u32,self+0x120));
    perm_hdRootAppendArchive(R_PERM_HEAP(u32,self+0x12C),R_PERM_HEAP(u32,self+0x11C));
    R_PERM_HEAP(u32,R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F8B08))+0x1C)=R_PERM_HEAP(u32,self+0x12C);
    const u32 first_mount=perm_hdRootCreateMount(0,0xFF);perm_name(frame+0x38,PERM_HEAP_DATA(0x100065C0));
    perm_hdRootMountDevice(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F8B08)),first_mount,frame+0x38);
    perm_nn_act_Initialize();const u32 slot=perm_nn_act_GetSlotNo();
    const u32 second_mount=perm_hdRootCreateMount(0,slot);perm_name(frame+0x40,PERM_HEAP_DATA(0x1000664C));
    perm_hdRootMountDevice(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F8B08)),second_mount,frame+0x40);
    perm_hdRootHeapInitialize(0);R_PERM_HEAP(u32,name_a+4)=PERM_HEAP_DATA(0x100063F8);
    const u32 heap_manager=R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F8B28));
    const u32 selected=R_PERM_HEAP(u32,self+0x44+R_PERM_HEAP(u32,self+0x58)*4);
    R_PERM_HEAP(u32,name_a)=PERM_HEAP_DATA(0x10006658);perm_hdRootSelectDefaultHeap(heap_manager,selected,name_a);
    const u32 info=perm_hdRootGetTaskInfo(self);R_PERM_HEAP(u32,R_PERM_HEAP(u32,info+0x1C)+0x3DC)=0;
    perm_graphics_heaps();perm_hdRootInputConfigure(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F5088)),PERM_HEAP_DATA(0x104A0CD8),0);
    const u32 heap_name=frame+0x18;perm_name(heap_name,PERM_HEAP_DATA(0x100065B0));
    u32 root=perm_hdRootGetMainHeap();const u32 resource_heap=perm_hdRootCreateExpHeap(0xB00000,heap_name,root,1,0);
    perm_hdRootResourceInitialize(resource_heap);perm_hdRootResourceSetHeap(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F4F7C)),resource_heap);
    const u32 command_size=perm_hdRootHeapOverhead(4)+0xE00000;
    perm_name(heap_name,PERM_HEAP_DATA(0x100065CC));root=perm_hdRootGetMainHeap();
    const u32 command_heap=perm_hdRootCreateExpHeap(command_size,heap_name,root,1,0),system_size=0x16C00000-command_size;
    perm_name(heap_name,PERM_HEAP_DATA(0x10006688));root=perm_hdRootGetMainHeap();
    const u32 system_heap=perm_hdRootCreateExpHeap(system_size,heap_name,root,1,1);
    const u32 data_size=perm_hdRootHeapOverhead(0x2000)+0x0D450000;
    perm_name(heap_name,PERM_HEAP_DATA(0x100065DC));const u32 data_heap=perm_hdRootCreateExpHeap(data_size,heap_name,system_heap,0xFFFFFFFFu,0);
    perm_hdRootModelInitialize(system_heap);const u32 save=R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F4F28));perm_hdRootSaveInitialize(save,system_heap);
    R_PERM_HEAP(u32,save+0x2044)=command_heap;R_PERM_HEAP(u32,save+0x2040)=system_heap;R_PERM_HEAP(u32,save+0x2128)=data_heap;
    const u32 archive_name=frame+0x20,archive_path=frame+0x48;
    perm_name(archive_name,PERM_HEAP_DATA(0x100065B8));root=perm_hdRootGetArchiveHeap();
    const u32 archive_heap=perm_hdRootCreateExpHeap(0x100000,archive_name,root,1,0);
    R_PERM_HEAP(u32,archive_path+4)=PERM_HEAP_DATA(0x100063F8);perm_name(archive_name,PERM_HEAP_DATA(0x100065B8));
    R_PERM_HEAP(u32,archive_path)=PERM_HEAP_DATA(0x100065F0);
    perm_hdRootLoadArchiveResource(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F4F7C)),archive_name,archive_path,archive_heap,1,0x40000,0x2000);
    perm_name(name_a,PERM_HEAP_DATA(0x100065B8));perm_name(name_b,PERM_HEAP_DATA(0x10006694));
    const u32 archive_resource=perm_hdRootGetArchiveResource(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F4F7C)),name_a,name_b,0);
    const u32 archive=perm_hdRootCastArchive(archive_resource),graphics_name=frame+0x50;
    perm_name(graphics_name,PERM_HEAP_DATA(0x10006600));root=perm_hdRootGetGraphicsHeap();
    const u32 graphics_heap=perm_hdRootCreateExpHeap(0x2000000,graphics_name,root,1,0);perm_hdRootUiGraphicsInitialize(graphics_heap);
    const u32 first_range=frame+0x28,second_range=frame+0x30;
    R_PERM_HEAP(u32,first_range+4)=0;R_PERM_HEAP(u32,first_range)=0;
    u32 files=perm_data_offset(archive+0x4C),file=perm_hdRootFindArchiveFile(files,PERM_HEAP_DATA(0x100066A0));
    u32 offset=R_PERM_HEAP(u32,file),size=R_PERM_HEAP(u32,file+4),first=offset?file+offset:0;
    R_PERM_HEAP(u32,second_range+4)=0;R_PERM_HEAP(u32,first_range+4)=first+size;R_PERM_HEAP(u32,second_range)=0;R_PERM_HEAP(u32,first_range)=first;
    files=perm_data_offset(archive+0x4C);file=perm_hdRootFindArchiveFile(files,PERM_HEAP_DATA(0x1000660C));
    offset=R_PERM_HEAP(u32,file);size=R_PERM_HEAP(u32,file+4);const u32 second=offset?file+offset:0;
    R_PERM_HEAP(u32,second_range)=second;R_PERM_HEAP(u32,second_range+4)=second+size;
    perm_hdRootSetShaderRanges(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x1018C404)),graphics_heap,first_range,second_range);
    const u32 ui_name=frame+0x58;perm_name(ui_name,PERM_HEAP_DATA(0x100066B8));root=perm_hdRootGetGraphicsHeap();
    const u32 ui_heap=perm_hdRootCreateExpHeap(0x5C00000,ui_name,root,1,0);perm_hdRootUiResourceInitialize(ui_heap);
    perm_hdRootUiResourceSetHeap(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F7274)),ui_heap);
    const u32 font_heap=perm_hdRootGetFontHeap();perm_hdRootFontInitialize(font_heap);perm_hdRootFontSetHeap(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x101F852C)),font_heap);
    perm_name(name_a,PERM_HEAP_DATA(0x100066C8));const u32 font_resource_heap=perm_hdRootCreateFrameHeap(0,name_a,font_heap,1,0);
    const u32 font_resource=perm_hdRootCreateFontResource(font_resource_heap);
    const u32 font_table=R_PERM_HEAP(u32,font_resource_heap+12);(void)PERM_HEAP_CALL1(R_PERM_HEAP(u32,font_table+44),font_resource_heap);
    perm_hdRootRegisterFontResource(font_resource);perm_hdRootMiiverseInitialize(0);perm_hdRootMiiverseConfigure(R_PERM_HEAP(u32,PERM_HEAP_DATA(0x1018F3F4)),0);
    const u32 factory=frame+0x60,source=frame+0x80,first_task=frame+0x100,second_task=frame+0x180;
    R_PERM_HEAP(u32,factory)=2;R_PERM_HEAP(u32,factory+4)=PERM_HEAP_DATA(0x0203FAF4);perm_hdRootTaskArgsConstruct(source,factory);
    const u32 child_heap=perm_hdRootGetChildHeap();u32 index=R_PERM_HEAP(u32,source+0x58);
    R_PERM_HEAP(u32,source+8+index*0x14)=child_heap;index=R_PERM_HEAP(u32,source+0x58);const u32 heap_spec=source+8+index*0x14;
    const u32 overhead=perm_hdRootHeapOverhead(4),profile=R_PERM_HEAP(u32,PERM_HEAP_DATA(0x104612E0));
    R_PERM_HEAP(u32,heap_spec+4)=0x04000000-overhead;R_PERM_HEAP(u32,source+0x5C)=self;R_PERM_HEAP(u32,source+0x60)=R_PERM_HEAP(u32,profile+0x14);
    u32 task=perm_create_task(self,source,first_task,PERM_HEAP_DATA(0x02760FE4));
    perm_cast_task(task,PERM_HEAP_DATA(0x101FD59C),PERM_HEAP_DATA(0x101FD9D4),PERM_HEAP_DATA(0x10006458));
    perm_hdRootBasePrepare(self);const u32 second_factory=frame+0x68;
    R_PERM_HEAP(u32,second_factory)=2;R_PERM_HEAP(u32,second_factory+4)=PERM_HEAP_DATA(0x0203FB48);perm_hdRootTaskArgsConstructAlternate(source,second_factory);
    task=perm_create_task(self,source,second_task,PERM_HEAP_DATA(0x02729488));
    perm_cast_task(task,PERM_HEAP_DATA(0x101FD71C),PERM_HEAP_DATA(0x101FD9D8),PERM_HEAP_DATA(0x10006448));
    process=perm_hdRootGetProcess();perm_hdRootSetProcessPhase(process,1);
}
