"""Hash guarded read-only instruction ranges; no patching of the executable."""
import sys,hashlib,pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from pathlib import Path
raw=Path(r'E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY/Binaries/ShippingPC-BmGame.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028'
p=pefile.PE(data=raw,fast_load=True);b=p.OPTIONAL_HEADER.ImageBase;img=p.get_memory_mapped_image();cs=Cs(CS_ARCH_X86,CS_MODE_32)
for arg in sys.argv[1:]:
 a,z=[int(s,16) for s in arg.split(':')]
 print('\nSTATIC',hex(a),hex(z))
 for i in cs.disasm(img[a-b:z-b],a):print(f'{i.address:08x} {i.bytes.hex():26} {i.mnemonic} {i.op_str}')
