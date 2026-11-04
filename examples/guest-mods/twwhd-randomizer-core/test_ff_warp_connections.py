"""Complete warp graph, both clearing passes and conditional FF links."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class FFWarpConnections(unittest.TestCase):
    def test_consumer(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char cells[49][0xA0];static u32 widgets[49][8];static unsigned present,finds,quadrants,links,sets,mutation,forwarded;
static const signed char coords[10][2]={{-2,-2},{0,-2},{2,-2},{-2,0},{1,0},{2,2},{-2,3},{-1,-1},{0,2},{-3,-3}};
static void* mem(u32 a){return(void*)a;}
#define R_FFW(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#include "ff_warp_connections.h"
static int number(int x,int y){assert(x>=-3&&x<=3&&y>=-3&&y<=3);return(y+3)*7+x+3;}
static int index_at(int x,int y){for(int i=0;i<10;i++)if(coords[i][0]==x&&coords[i][1]==y)return i;return -1;}
static unsigned cell_number(u32 cell){assert(cell>=(u32)cells&&cell<(u32)cells+sizeof cells);return(unsigned)(cell-(u32)cells)/0xA0;}
u32 rfc_quadrant(u32 i){assert(i<10);quadrants++;return((u32)(u8)coords[i][0]<<24)|((u32)(u8)coords[i][1]<<16)|0xDEAD;}
u32 rfc_find(u32 grid,u32 coordinate){assert(grid==77);const int x=*(signed char*)coordinate,y=*((signed char*)coordinate+1);finds++;if(mutation==1&&finds==1)return 0;const int i=index_at(x,y);return i>=0&&!(present&(1u<<i))?0:(u32)cells[number(x,y)];}
void rfc_widget(u32 cell,u32 direction,u32 widget){const unsigned n=cell_number(cell);assert(direction<8);sets++;if(mutation==2&&widget){const unsigned m=number(-2,-2);if(forwarded++)assert(widget==999);else{assert(widget==100+m);R_FFW(u32,(u32)cells[m]+0x9C)=999;}}widgets[n][direction]=widget;}
void rfc_adjacent(u32 grid,u32 cell,u32 coordinate,u32 direction){assert(grid==77&&direction<8);links++;const int x=*(signed char*)coordinate,y=*((signed char*)coordinate+1);const int i=index_at(x,y);assert(i>=0);const u32 widget=present&(1u<<i)?100+number(x,y):0;widgets[cell_number(cell)][direction]=widget;}
static const unsigned edges[][3]={{0,1,7},{0,7,3},{0,7,5},{0,9,1},{9,0,5},{1,0,6},{1,2,7},{1,7,3},{1,7,4},{2,1,6},{2,7,4},{2,7,3},{7,0,1},{7,0,0},{7,1,2},{7,3,4},{7,3,3},{7,4,5},{3,7,0},{3,7,2},{3,4,7},{3,8,3},{3,8,5},{4,7,1},{4,7,0},{4,3,6},{4,8,4},{4,8,3},{4,5,5},{8,3,1},{8,3,0},{8,4,2},{8,5,7},{8,6,4},{8,6,3},{8,6,6},{5,4,1},{5,4,0},{5,8,6},{6,8,0},{6,8,2},{6,8,7}};
static void reset(unsigned mask){memset(cells,0,sizeof cells);present=mask;finds=quadrants=links=sets=mutation=forwarded=0;for(unsigned n=0;n<49;n++){R_FFW(u8,(u32)cells[n]+0x9A)=7;R_FFW(u32,(u32)cells[n]+0x9C)=100+n;for(unsigned d=0;d<8;d++)widgets[n][d]=777;}}
int main(void){unsigned cases=0;
 for(unsigned mask=0;mask<1024;mask++){
  reset(mask);randomizer_ff_warp_connections(77);u32 expected[49][8];unsigned enabled[49],expected_links=0;
  for(unsigned n=0;n<49;n++){enabled[n]=7;for(unsigned d=0;d<8;d++)expected[n][d]=777;}
  if(mask&1)for(int y=-3;y<=3;y++)for(int x=-3;x<=3;x++){
   const int n=number(x,y),i=index_at(x,y);if(i>=0&&!(mask&(1u<<i)))continue;if(i==0)continue;enabled[n]=0;for(unsigned d=0;d<8;d++)expected[n][d]=100+number(-2,-2);
  }
  for(unsigned i=0;i<10;i++)if(mask&(1u<<i))for(unsigned d=0;d<8;d++)expected[number(coords[i][0],coords[i][1])][d]=0;
  for(unsigned e=0;e<sizeof edges/sizeof*edges;e++){
   const unsigned from=edges[e][0],to=edges[e][1],dir=edges[e][2];if(!(mask&(1u<<from))||(from==9&&!(mask&1)))continue;const int n=number(coords[from][0],coords[from][1]);enabled[n]=1;expected[n][dir]=mask&(1u<<to)?100+number(coords[to][0],coords[to][1]):0;expected_links++;
  }
  for(unsigned n=0;n<49;n++){assert(R_FFW(u8,(u32)cells[n]+0x9A)==enabled[n]);for(unsigned d=0;d<8;d++)assert(widgets[n][d]==expected[n][d]);}assert(links==expected_links);cases++;
 }
 reset(1023);mutation=1;randomizer_ff_warp_connections(77);assert(links==42&&R_FFW(u8,(u32)cells[number(3,3)]+0x9A)==7&&R_FFW(u8,(u32)cells[0]+0x9A)==1);cases++;
 reset(1023);mutation=2;randomizer_ff_warp_connections(77);assert(forwarded==48*8&&links==42);cases++;
 printf("%u complete FF warp connection cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(source)
            subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
