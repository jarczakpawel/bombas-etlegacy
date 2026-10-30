#!/usr/bin/env python3
import argparse
import hashlib
import json
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--artifacts', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()

required = {
    'linux64': 'cgame.mp.x86_64.so',
    'linux32': 'cgame.mp.i386.so',
    'windows64': 'cgame_mp_x64.dll',
    'windows32': 'cgame_mp_x86.dll',
    'macos': 'cgame_mac',
}
files = {}
checksums = {}
for platform, filename in required.items():
    path = args.artifacts / platform / filename
    if not path.is_file() or path.is_symlink():
        raise FileNotFoundError(path)
    data = path.read_bytes()
    if len(data) < 100000:
        raise ValueError(f'Incomplete native module: {path}')
    if platform.startswith('windows') and not data.startswith(b'MZ'):
        raise ValueError(f'Invalid PE binary: {path}')
    if platform.startswith('linux') and not data.startswith(b'\x7fELF'):
        raise ValueError(f'Invalid ELF binary: {path}')
    if platform == 'macos' and data[:4] not in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'PK\x03\x04'):
        raise ValueError(f'Invalid macOS binary or bundle: {path}')
    if platform == 'macos' and data.startswith(b'PK\x03\x04'):
        with zipfile.ZipFile(path) as packed:
            if packed.testzip() is not None or not any(x.endswith('/Contents/MacOS/cgame_mac') for x in packed.namelist()):
                raise ValueError(f'Invalid macOS bundle: {path}')
    files[filename] = data
    checksums[platform] = hashlib.sha256(data).hexdigest()

for name in ('menus.txt', 'ingame_main.menu', 'ingame_bombas.menu'):
    path = Path(__file__).resolve().parents[2] / 'etmain' / 'ui' / name
    files['ui/' + name] = path.read_bytes()

for name in ('pt1_strt.wav', 'pt1_end.wav'):
    path = Path(__file__).parent / 'assets' / name
    payload = path.read_bytes()
    if payload[:4] != b'RIFF':
        raise ValueError(f'Invalid sound asset: {path}')
    files['sound/movers/plats/' + name] = payload

files['bombas/client_manifest.json'] = (json.dumps({
    'version': 84,
    'supported_platforms': list(required),
    'native_sha256': checksums,
    'requires_installer': False,
}, sort_keys=True, indent=2) + '\n').encode()

args.output.mkdir(parents=True, exist_ok=True)
temporary = args.output / 'bombas-client-universal.pk3'
with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=8) as archive:
    for name, content in sorted(files.items()):
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o100644 << 16
        archive.writestr(info, content)
with zipfile.ZipFile(temporary) as archive:
    if archive.testzip() is not None or set(archive.namelist()) != set(files):
        raise ValueError('Client package verification failed')
digest = hashlib.sha256(temporary.read_bytes()).hexdigest()
result = args.output / f'zzzzzz_bombas_client_universal_v084_{digest[:12]}.pk3'
temporary.rename(result)
print(json.dumps({'file': result.name, 'sha256': digest, 'files': sorted(files), 'native_sha256': checksums}, indent=2))
