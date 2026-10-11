"""Follow command-description pointers to vtables and queued render roots."""
from pathlib import Path
import hashlib,json,struct
import pefile,capstone
raw=Path(r'E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY/Binaries/ShippingPC-BmGame.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028'
pe=pefile.PE(data=raw,fast_load=True);img=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
def refs(va):
 start=0;result=[]
 while (start:=img.find(struct.pack('<I',va),start))>=0:result.append(base+start);start+=1
 return result
def parent(va):
 at=img.rfind(b'\xcc\xcc',max(0,va-base-0x10000),va-base)
 if at<0:return None
 while img[at]==0xcc:at+=1
 return base+at
profile=json.loads(Path(__file__).with_name('evidence').joinpath('target-profile.json').read_text())
out=[]
for name in ['BeginDrawingCommand','EndDrawingCommand']:
 for entry in profile['ue3_leads'][name]:
  for lead in entry['text_pointer_byte_leads']:
   describe=int(lead,16)-1
   if img[describe-base]!=0xb8:continue
   for ptr in refs(describe):
    if not 0x1e29000<=ptr<0x25d5000:continue
    table=ptr-8;execute=struct.unpack_from('<I',img,ptr-base-4)[0]
    if not base<=execute<0x1e29000:continue
    target={'name':name,'describe':hex(describe),'table':hex(table),'execute':hex(execute),
       'execute_prefix':img[execute-base:execute-base+16].hex(),'root_leads':[]}
    for use in refs(table):
     if not base<=use<0x1e29000:continue
     start=parent(use)
     target['root_leads'].append({'xref':hex(use),'candidate':hex(start) if start else None})
    out.append(target)
Path(__file__).with_name('evidence').joinpath('queued-render-map.json').write_text(json.dumps({'grade':'STATIC','commands':out},indent=2))
print(json.dumps(out,indent=2))
