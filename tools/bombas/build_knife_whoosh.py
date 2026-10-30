import subprocess
from pathlib import Path

root = Path(__file__).parent
subprocess.run([
    'ffmpeg', '-nostdin', '-v', 'error', '-y',
    '-i', str(root / 'audio_sources/802464_sadiquecat_stick_whoosh_6_cc0.mp3'),
    '-af', 'volume=0.85', '-ac', '1', '-ar', '22050',
    '-c:a', 'pcm_s16le', '-map_metadata', '-1',
    str(root / 'assets/knife_whoosh_6.wav'),
], check=True)
