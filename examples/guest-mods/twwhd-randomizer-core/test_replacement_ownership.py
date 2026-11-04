"""Each native function has one active source replacement in the full module."""
from pathlib import Path
import re,unittest
class ReplacementOwnership(unittest.TestCase):
 def test_unique_replacements(self):
  root=Path(__file__).resolve().parent
  visited=set();replacements={}
  def visit(path):
   if path in visited:return
   visited.add(path);source=path.read_text()
   for match in re.finditer(r'WWHD_REPLACE\(\s*(0x[0-9A-Fa-f]+)\s*,\s*\w+\s*,\s*(\w+)',source):
    address=int(match[1],16)
    replacements.setdefault(address,[]).append(f'{path.name}:{match[2]}')
   for name in re.findall(r'^\s*#include\s+"([^"]+)"',source,re.MULTILINE):
    included=path.parent/name
    if included.is_file():visit(included.resolve())
  visit(root/'mod.c')
  self.assertGreater(len(replacements),100)
  self.assertEqual({hex(a):owners for a,owners in replacements.items() if len(owners)>1},{})
if __name__=='__main__':unittest.main()
