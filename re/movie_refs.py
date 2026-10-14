"""Read-only Bink import callers in the exact Asylum binary."""
from pathlib import Path
import hashlib, pefile, struct, json
raw=Path(r'E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY/Binaries/ShippingPC-BmGame.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028'
pe=pefile.PE(data=raw,fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
base=pe.OPTIONAL_HEADER.ImageBase
image=pe.get_memory_mapped_image()
for dll in pe.DIRECTORY_ENTRY_IMPORT:
 for i in dll.imports:
  if i.name!=b'_BinkOpen@8': continue
  needle=b'\xff\x15'+struct.pack('<I',i.address)
  at=0
  while True:
   at=image.find(needle,at)
   if at<0:break
   boundary=image.rfind(b'\xcc\xcc\xcc',max(0,at-16000),at)
   while boundary>=0 and image[boundary]==0xcc:boundary+=1
   print(json.dumps({'iat':hex(i.address),'call':hex(base+at),'candidate_parent':hex(base+boundary)}))
   at+=len(needle)
