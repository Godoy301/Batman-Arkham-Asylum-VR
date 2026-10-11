"""Bounded render-command references; function starts remain static leads."""
from pathlib import Path
import hashlib,json,struct
import pefile,capstone
exe=Path(r'E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY/Binaries/ShippingPC-BmGame.exe')
raw=exe.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028'
pe=pefile.PE(data=raw,fast_load=True);img=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
def refs(va):
    start=0;res=[]
    while (start:=img.find(struct.pack('<I',va),start))>=0:
        res.append(base+start);start+=1
    return res
def parent(va):
    off=va-base
    at=img.rfind(b'\xcc\xcc',max(0,off-0x10000),off)
    if at<0:return None
    while img[at]==0xcc:at+=1
    return at+base
out={}
for va in [0x2085ed4,0x2085ee0,0x1eb40d8]:
    hits=refs(va);entries=[]
    for hit in hits:
        start=parent(hit)
        entries.append({'reference':hex(hit),'function_boundary_candidate':hex(start) if start else None,
          'first_16_bytes':img[start-base:start-base+16].hex() if start else '',
          'near_instructions':[f'{i.address:08x} {i.mnemonic} {i.op_str}' for i in cs.disasm(img[start-base:start-base+800],start)][:90] if start else []})
    out[hex(va)]=entries
Path(__file__).with_name('evidence').joinpath('render-function-leads.json').write_text(json.dumps({'grade':'STATIC','warning':'Raw pointer matches and CC-padding candidates are not proven function starts or live callers','leads':out},indent=2))
print(json.dumps({k:[{x:y for x,y in e.items() if x!='near_instructions'} for e in v] for k,v in out.items()},indent=2))
