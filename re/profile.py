"""Read-only exact-build PE profile and UE3 string-reference leads."""
import hashlib
import json
import struct
from pathlib import Path
import pefile

exe=Path(r'E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY/Binaries/ShippingPC-BmGame.exe')
sha='4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028'
raw=exe.read_bytes()
assert hashlib.sha256(raw).hexdigest()==sha, 'Unsupported Asylum executable'
p=pefile.PE(data=raw)
base=p.OPTIONAL_HEADER.ImageBase
img=p.get_memory_mapped_image()
text=next(s for s in p.sections if s.Name.startswith(b'.text'))
lo,hi=text.VirtualAddress,text.VirtualAddress+text.Misc_VirtualSize
def hits(data,pattern):
    at=data.find(pattern)
    while at>=0:
        yield at
        at=data.find(pattern,at+1)
leads={}
for key in ['BeginDrawingCommand','EndDrawingCommand','UGameViewportClient',
            'UCanvasexecProject','FSceneView','CalcSceneView','AllocateViewState',
            'SceneView.cpp','GameViewportClient.cpp','Bink','Play_UI_Fader_Bats']:
    result=[]
    for enc in ['ascii','utf-16le']:
        for at in list(hits(img,key.encode(enc)))[:15]:
            va=base+at
            refs=[base+lo+r for r in hits(img[lo:hi],struct.pack('<I',va))]
            result.append({'encoding':enc,'va':hex(va),'text_pointer_byte_leads':[hex(r) for r in refs[:40]]})
    leads[key]=result
records={
 'grade':'STATIC','exe':str(exe),'sha256':sha,'architecture':'x86',
 'image_base':hex(base),'image_size':p.OPTIONAL_HEADER.SizeOfImage,
 'entry_rva':hex(p.OPTIONAL_HEADER.AddressOfEntryPoint),
 'timestamp':p.FILE_HEADER.TimeDateStamp,
 'sections':[{'name':s.Name.rstrip(b'\0').decode(),'rva':hex(s.VirtualAddress),'size':s.Misc_VirtualSize} for s in p.sections],
 'imports':{d.dll.decode():[x.name.decode() if x.name else f'ordinal:{x.ordinal}' for x in d.imports] for d in p.DIRECTORY_ENTRY_IMPORT},
 'warning':'Imports are static API leads. Pointer byte matches are not proven xrefs or function starts.',
 'ue3_leads':leads}
out=Path(__file__).parent/'evidence/target-profile.json'
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(records,indent=2))
print(json.dumps({'hash':sha,'image_base':hex(base),'entry_rva':records['entry_rva'],
 'leads':{k: v for k,v in leads.items() if any(x['text_pointer_byte_leads'] for x in v)}},indent=2))
