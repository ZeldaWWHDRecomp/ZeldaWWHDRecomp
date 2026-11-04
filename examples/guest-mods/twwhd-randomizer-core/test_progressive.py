import os
from pathlib import Path
import ctypes,json,subprocess,tempfile
root=Path(__file__).resolve().parent
header=root/"progressive_ids.h"
# Reference progression graphs from the upstream progressive handlers, including saturation.
groups=[
 ([0x38,0x39,0x3A,0x3D,0x3E],0xD4,{0:0x38,1:0x39,3:0x3A,7:0x3E}),
 ([0x3B,0x3C],0xD5,{0:0x3B,1:0x3C}),
 ([0x27,0x35,0x36],0x7D,{0:0x27,1:0x35,3:0x36}),
 ([0xAB,0xAC],0x32,{0:0xAB,1:0xAC}),
 ([0xAD,0xAE],0x90,{30:0xAD,60:0xAE}),
 ([0xAF,0xB0],0x8F,{30:0xAF,60:0xB0}),
 ([0x23,0x26],0x79,{0:0x23,1:0x26}),
 ([0x78,0x77],0x5D,{255:0x78,0x78:0x77}),
 ([0xB1,0xB2],0x33,{0:0xB1,16:0xB2})]
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);src=tmp/"driver.c";lib=tmp/"driver.dylib"
 src.write_text('#include \"'+str(root/'field_item_logic.h')+'\"\nfloat field_y(unsigned item){return rando_field_y_offset(item); }\n#include "'+str(header)+'"\nunsigned next_id(unsigned item,unsigned state,unsigned magic) {return rando_progressive_next(item,state,magic); }\nunsigned field(unsigned item){return rando_progressive_field(item); }\nunsigned display(unsigned item,unsigned state,unsigned magic){return rando_progressive_display(item,state,magic); }\n')
 subprocess.run([os.environ.get("CC", "cc"),"-O2","-shared","-fPIC",str(src),"-o",str(lib)],check=True)
 dll=ctypes.CDLL(str(lib))
 try:
  dll.next_id.argtypes=[ctypes.c_uint]*3;dll.next_id.restype=ctypes.c_uint;dll.field.argtypes=[ctypes.c_uint];dll.field.restype=ctypes.c_uint
  dll.display.argtypes=[ctypes.c_uint]*3;dll.display.restype=ctypes.c_uint
  dll.field_y.argtypes=[ctypes.c_uint];dll.field_y.restype=ctypes.c_float
  for item,expected in [(0,0),(0x1A,0),(0x1F,0),(0x20,23),(0x37,23),(0x38,20),(0x39,23),(0x3D,23),(0x44,23),(0x45,0),(0x4B,0),(0x4C,23),(0xFF,23)]:
   assert dll.field_y(item)==expected,(hex(item),expected)
  cases=0;display_cases=0
  for items,field,graph in groups:
   for item in items:
    assert dll.field(item)==field
    for state in range(256):
     assert dll.next_id(item,state,0)==graph.get(state,255),(hex(item),state)
     cases+=1
     assert dll.display(item,state,0)==graph.get(state,items[0]),(hex(item),state)
     display_cases+=1
    if field==0x33:
     for state in range(256):
      assert dll.next_id(item,state,1)==0xB2
      cases+=1
      assert dll.display(item,state,1)==0xB2
      display_cases+=1
  ordinary=set(range(255))-{i for items,_,_ in groups for i in items}
  for item in ordinary:
   assert dll.field(item)==0xFFFFFFFF
   for state in (0,1,255):
    assert dll.next_id(item,state,0)==item
    cases+=1
    assert dll.display(item,state,0)==item
    display_cases+=1
  result=dict(asserted_selection_cases=cases,asserted_presentation_cases=display_cases,field_height_boundary_cases=13,progressive_families=len(groups),all_aliases=True,all_byte_states=True,saturated_and_invalid_states_do_not_grant=True,ordinary_item_ids_unchanged=True)
  print(json.dumps(result,indent=2))
 finally:
  # Windows locks loaded DLLs even when an assertion or ctypes call fails.
  if os.name == "nt":
   import _ctypes
   _ctypes.FreeLibrary(dll._handle)
  del dll
