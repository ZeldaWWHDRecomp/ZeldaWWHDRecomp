"""Related figurines retain source priority and captured selection count."""
from pathlib import Path
import subprocess,tempfile,unittest
class GalleryFigures(unittest.TestCase):
 def test_grants(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
typedef uintptr_t u32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "gallery_figures.h"
static unsigned count,initial,counts,gets,n,mode,figure;static u32 granted[4096];
u32 randomizer_gallery_count(u32 a){assert(a==123);counts++;return count;}
u32 randomizer_gallery_photo(u32 a,u32 i){assert(a==123&&i==(gets&255));gets++;return figure;}
void randomizer_gallery_set_figure(u32 a,u32 f){assert(a==123&&n<4096);granted[n++]=f;count=0;}
u32 randomizer_gallery_index(u32 p){unsigned value=(p*37+19)&255;if(mode==1&&(p==0x99||p==0x4A))value=(0xBA*37+19)&255;return value|0xABCD0000;}
static unsigned low(unsigned p){return randomizer_gallery_index(p)&255;}
int main(void){unsigned lengths[]={0,1,3,255,256,260};for(mode=0;mode<2;mode++)for(figure=0;figure<258;figure++)for(unsigned c=0;c<6;c++){
 count=initial=lengths[c];counts=gets=n=0;randomizer_gallery_figures(123);assert(counts==1&&gets==initial);
 unsigned extra[8],k=0;const unsigned source[]={0xBA,0x99,0xCE,0xC4,0x4A,0x88,0x8C};const unsigned dest[]={0xC6,0x9A,0xCA,0xC5,0x49,0,0x8D};
 for(unsigned j=0;j<7;j++)if(figure==low(source[j])){if(j==5){for(unsigned p=0x73;p<=0x78;p++)extra[k++]=low(p);extra[k++]=low(0x72);extra[k++]=low(0x87);}else extra[k++]=low(dest[j]);break;}
 assert(n==initial*(k+1));for(unsigned i=0;i<initial;i++){assert(granted[i*(k+1)]==figure);for(unsigned j=0;j<k;j++)assert(granted[i*(k+1)+1+j]==extra[j]);}
 }return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
