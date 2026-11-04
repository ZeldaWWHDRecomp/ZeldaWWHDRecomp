from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class WindfallPedestals(unittest.TestCase):
    def test_phases_constructor_and_callback_order(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "windfall_pedestals.h"
static _Alignas(uintptr_t) unsigned char actor[0xA00];
static u32 step, constructed, query_result, heap_result, mutate;
static s32 first, second;
static u32 base(void){return (u32)actor;}
static u32 word(u32 off){return *(uint32_t*)(actor+off);}
static void put(u32 off,u32 value){*(uint32_t*)(actor+off)=(uint32_t)value;}
void randomizer_pedestal_npc_ctor(u32 self){assert(self==base()&&step++==0);if(mutate)put(0x2E4,0x100);}
void randomizer_pedestal_status_ctor(u32 self){assert(self==base()+0x7F0&&step++==1&&word(0xB4)==0x1000CC70);}
void randomizer_pedestal_gstatus_ctor(u32 self){assert(self==base()+0x80C&&step++==2);}
void randomizer_pedestal_collision_ctor(u32 self){assert(self==base()+0x82C&&step++==3&&word(0x808)==0x1004AE88&&word(0x80C)==0x1004AEC0);}
void randomizer_pedestal_cylinder_ctor(u32 self){assert(self==base()+0x944&&step++==4&&word(0x940)==0x100015A8&&word(0x93C)==0x1000CBCC);}
s32 randomizer_pedestal_bag_query(u8 item,s32 flag){
 assert(item==0x30&&flag==1&&step++==(constructed?0u:5u));
 assert(word(0x2E4)==(constructed?0x28u:mutate?0x108u:0x28u));
 if(!constructed)assert(word(0x868)==0x1004B108&&word(0x940)==0x1004B160&&word(0x958)==0x1004B150);
 return query_result;
}
s32 randomizer_pedestal_load(u32 self,u32 name){
 const u32 index=step++-(constructed?1u:6u);
 assert(index<2&&self==base()+(index?0x7E4:0x7DC)&&name==(index?0x1000CC60u:0x1000CC68u));
 return index?second:first;
}
s32 randomizer_pedestal_heap(u32 self,u32 callback,u32 size){assert(self==base()&&callback==0x02115E78&&size==0x4C0&&step++==(constructed?3u:8u));return heap_result;}
void randomizer_pedestal_init(u32 self){assert(self==base()&&step++==(constructed?4u:9u));}
int main(void){
 for(constructed=0;constructed<2;constructed++)for(query_result=0;query_result<2;query_result++)
 for(first=-2;first<7;first++)for(second=-2;second<7;second++)
 for(heap_result=0;heap_result<2;heap_result++)for(mutate=0;mutate<2;mutate++){
  memset(actor,0,sizeof(actor));put(0x2E4,constructed?0x28:0x20);step=0;
  s32 expected=first!=4?first:second!=4?second:heap_result?4:5;
  assert(randomizer_pedestal_create(base())==expected);
  u32 tail=first!=4?2:second!=4?3:heap_result?5:4;
  assert(step==tail+(constructed?0u:5u));
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # Host pointers are wide; guest fields remain exactly four bytes.
            header = Path(__file__).with_name('windfall_pedestals.h').read_text()
            (root/'windfall_pedestals.h').write_text(header.replace('volatile u32*', 'volatile uint32_t*'))
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                            str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
