"""Freeze the reusable Origins O1H source, without copying its game hooks."""
from pathlib import Path
import hashlib
import json
import shutil

root = Path(__file__).resolve().parents[2]
src, dst = root / 'batman_origins', root / 'batman_asylum'
if (dst/'src/core/framework/framework.cpp').exists():
    raise SystemExit('Source already imported; refusing to overwrite Asylum integration changes.')
ignore = shutil.ignore_patterns('.git', 'build', '__pycache__', '*.pdb', '*.obj')
files = []
for folder in ['src/core', 'third_party', 'helper64']:
    for p in (src/folder).rglob('*'):
        if p.is_file() and '.git' not in p.parts and 'build' not in p.parts:
            files.append({'path':str(p.relative_to(src)).replace('\\','/'),
                          'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    shutil.copytree(src/folder, dst/folder, ignore=ignore, dirs_exist_ok=True)
for name in ['src/game/igame_adapter.h', 'src/game/adapter_registry.h',
             'src/proxy.cpp', 'src/proxy.def', 'src/hash.cpp', 'src/probe.h',
             'LICENSE-bioshock-vr.txt', 'LICENSE-luma-framework.md',
             'tools/xr_config32.cpp']:
    p, q = src/name, dst/name
    q.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p,q)
for folder in ['src', 'helper64']:
    for p in (dst/folder).rglob('*'):
        if p.suffix not in ['.cpp','.h','.txt','.def']: continue
        text = p.read_text(encoding='utf-8')
        for a,b in [('game/origins','game/asylum'),('bvr::bo','bvr::aa'),
                    ('namespace bo','namespace aa'),('bo::HashFile','aa::HashFile'),
                    ('Batman_Origins','Batman_Asylum'),('batman_origins','batman_asylum'),
                    ('BatmanOriginsDLSS','BatmanAsylumDLSS'),('bo_dlss64','aa_dlss64'),
                    ('-bovr','-bavr')]: text=text.replace(a,b)
        p.write_text(text,encoding='utf-8')
(dst/'re/evidence').mkdir(parents=True,exist_ok=True)
(dst/'re/evidence/donor-source.json').write_text(json.dumps({
    'donor':'Origins O1H','grade':'SOURCE','files':files,
    'note':'No Origins engine, allocator, shader or movie hook copied/linked.'},indent=2))
print('Frozen Origins core/dependencies/input/DLSS-helper source in isolated Asylum project.')
