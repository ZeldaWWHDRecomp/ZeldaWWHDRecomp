"""Fixed traversal relocation/data validation and narrow startup writes."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from traversal_seed_data import (RELOCATIONS, ADDI, FLOAT_TARGETS, BLOCK_BASE,
                                 BLOCK_STRIDE, BLOCK_COUNT, GRAPPLE_FLOAT,
                                 validate_traversal_seed_data)

class TraversalSeedData(unittest.TestCase):
    def images(self):
        original, seed = {}, {}
        for address, (old, new) in RELOCATIONS.items():
            original[address], seed[address] = old.to_bytes(4, 'big'), new.to_bytes(4, 'big')
        for address, (first, base, old, new) in ADDI.items():
            original[address] = ((14<<26)|(first<<21)|(base<<16)|old).to_bytes(4, 'big')
            seed[address] = ((14<<26)|(first<<21)|(base<<16)|new).to_bytes(4, 'big')
        for address, value in FLOAT_TARGETS.items():
            original[address] = seed[address] = struct.pack('>f', value)
        original[GRAPPLE_FLOAT], seed[GRAPPLE_FLOAT] = struct.pack('>f',17), struct.pack('>f',25)
        for index in range(BLOCK_COUNT):
            address = BLOCK_BASE + index*BLOCK_STRIDE
            original[address] = bytes((n+index)&255 for n in range(BLOCK_STRIDE))
            row = bytearray(original[address]);row[4:6] = row[10:12] = (12).to_bytes(2,'big')
            seed[address] = bytes(row)
        return original, seed

    def test_exact_policies(self):
        original, seed = self.images()
        def reader(values):
            def read(address,size):
                result=values[address]
                self.assertEqual(len(result),size)
                return result
            return read
        validate_traversal_seed_data(reader(original),reader(seed))
        for image in (original,seed):
            for address, value in list(image.items()):
                for byte in range(len(value)):
                    changed=bytearray(value);changed[byte]^=1;image[address]=bytes(changed)
                    if image is original and BLOCK_BASE <= address < BLOCK_BASE+BLOCK_COUNT*BLOCK_STRIDE and byte in (4,5,10,11):
                        validate_traversal_seed_data(lambda a,n:original[a],lambda a,n:seed[a])
                    else:
                        with self.assertRaises(ValueError):
                            validate_traversal_seed_data(lambda a,n:original[a],lambda a,n:seed[a])
                image[address]=value
        for address in seed:
            old=seed[address]
            for short in (b'',old[:-1],old+b'\0'):
                seed[address]=short
                with self.assertRaises(ValueError):
                    validate_traversal_seed_data(lambda a,n:original[a],lambda a,n:seed[a])
            seed[address]=old

    def test_startup_changes_only_reviewed_fields(self):
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef float f32;
static unsigned char rows[13*0x9C];static f32 grapple;
static void* data(u32 a){if(a==0x100110C8)return &grapple;assert(a==0x101CB424);return rows;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#define WWHD_HOOK(a,n,p) void n p
#include "traversal_seed_data.h"
int main(void){for(unsigned value=0;value<256;value++){
 memset(rows,value,sizeof rows);grapple=value;randomizer_initialize_traversal_data();assert(grapple==25);
 for(unsigned row=0;row<13;row++)for(unsigned i=0;i<0x9C;i++){
 const unsigned offset=i==4||i==5?4:i==10||i==11?10:0;
 if(offset)assert(*(u16*)(rows+row*0x9C+offset)==12);else assert(rows[row*0x9C+i]==value);
 }
 randomizer_initialize_traversal_data();assert(grapple==25);
}return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(code)
            subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
