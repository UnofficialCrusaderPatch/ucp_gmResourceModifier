"""Execute the owner's Lua initialization against real reference PE images.

Set SHC_REFERENCE_DIR, UCP_FRAMEWORK_DIR and optional SHC_VARIANT_DIRS (semicolon
separated directories with the same normal/Extreme filenames).
"""
import hashlib
import os
from pathlib import Path
import re
import struct
import unittest

import pefile
from lupa.lua54 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK = Path(os.environ.get('UCP_FRAMEWORK_DIR', str(ROOT.parent/'UnofficialCrusaderPatch3')))
REFERENCE = Path(os.environ.get('SHC_REFERENCE_DIR', 'C:/UCPTools/Original-SHC141'))


class BindingHost:
    def __init__(self, path):
        pe = pefile.PE(str(path))
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.image = bytearray(pe.get_memory_mapped_image())
        self.writes = []
        self.scans = []
        self.lua = LuaRuntime(encoding=None, unpack_returned_tuples=True)
        g = self.lua.globals()
        self.lua.execute(b'core={}; data={}; log=function() end; DEBUG=1; WARNING=-1; ERROR=-2')
        g.core.scanForAOB = self.scan
        g.core.readInteger = lambda address: struct.unpack_from('<i', self.image, address-self.base)[0]
        g.core.writeCode = lambda address, values: self.writes.append((address, list(values.values())))
        code = FRAMEWORK/'content/ucp/code'
        g.data.cache = self.lua.execute((code/'data/cache.lua').read_bytes())
        source = (code/'core.lua').read_bytes()
        self.lua.execute(source[source.index(b'function core.AOBScan('):source.index(b'---Hook game code')])
        # Replace only DLL loading. Production bindings and the real framework
        # cache execute; addresses are captured instead of patching this process.
        self.lua.execute(b'''
          exports=setmetatable({}, {__index=function(t,k)
            local v=0x7000000; for _ in pairs(t) do v=v+4 end
            rawset(t,k,v); return v
          end})
          require=function(name) assert(name=='gmResourceModifier.dll'); return exports end
        ''')
        self.module = self.lua.execute((ROOT/'init.lua').read_bytes())

    def scan(self, pattern, start=None, stop=None):
        regex = b''.join(b'.' if x==b'?' else re.escape(bytes([int(x,16)])) for x in pattern.split())
        lower = max((start or self.base)-self.base, 0)
        upper = min((stop or (self.base+len(self.image)))-self.base, len(self.image))
        matches = list(re.finditer(regex, self.image[lower:upper], re.DOTALL))
        self.scans.append((pattern, [self.base+lower+m.start() for m in matches]))
        return self.base+lower+matches[0].start() if matches else None

    def enable(self):
        self.module.enable(self.module, self.lua.table(), self.lua.table())


class Bindings(unittest.TestCase):
    def test_required_families_and_available_official_variants(self):
        dirs = [REFERENCE] + [Path(p) for p in os.environ.get('SHC_VARIANT_DIRS','').split(';') if p]
        for directory in dirs:
            for name in ('Stronghold Crusader.exe', 'Stronghold_Crusader_Extreme.exe'):
                path = directory/name
                with self.subTest(path=path):
                    h = BindingHost(path); h.enable()
                    self.assertEqual(len(h.scans), 7)
                    self.assertTrue(all(len(matches)==1 for _,matches in h.scans))
                    loader = h.scans[1][1][0]
                    call = h.scans[0][1][0]
                    self.assertEqual(call+5+struct.unpack_from('<i',h.image,call-h.base+1)[0], loader)
                    writes = dict(h.writes)
                    for name,offset in ((b'address_ShcFirstImageStart',0x72),(b'address_ShcGmCount',0x36)):
                        expected = struct.unpack_from('<i',h.image,loader-h.base+offset)[0]
                        self.assertEqual(writes[h.lua.globals().exports[name]],[expected])
                    print(path, hashlib.sha256(path.read_bytes()).hexdigest())

    def test_changed_context_and_occupied_call_fail_before_any_write(self):
        for extreme in (False,True):
            name = 'Stronghold_Crusader_Extreme.exe' if extreme else 'Stronghold Crusader.exe'
            for change in ('missing-loader','changed-capacity','occupied-call','earlier-loader-copy'):
                with self.subTest(extreme=extreme,change=change):
                    h = BindingHost(REFERENCE/name)
                    # First resolve through the real cache, then change live bytes.
                    h.enable(); call=h.scans[0][1][0]; loader=h.scans[1][1][0]
                    h.writes.clear()
                    if change=='missing-loader': h.image[loader-h.base]=0xe9
                    elif change=='changed-capacity': h.image[loader-h.base+0x2c]=0xef
                    elif change=='occupied-call':
                        off=call-h.base+1
                        target=struct.unpack_from('<i',h.image,off)[0]
                        struct.pack_into('<i',h.image,off,target+16)
                    else:
                        # Cached discovery may still select the actual function;
                        # its decoded caller disambiguates it. Cold discovery of
                        # an earlier copy must not redirect that caller.
                        h.image[0x1000:0x1000+0x9f]=h.image[loader-h.base:loader-h.base+0x9f]
                        h.lua.globals().data.cache=h.lua.execute((FRAMEWORK/'content/ucp/code/data/cache.lua').read_bytes())
                    with self.assertRaises(Exception): h.enable()
                    self.assertEqual(h.writes,[])


if __name__=='__main__': unittest.main()
