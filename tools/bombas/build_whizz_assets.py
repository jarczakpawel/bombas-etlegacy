import subprocess
import wave
from pathlib import Path

import numpy as np

source = Path(__file__).parent / 'audio_sources'
output = Path(__file__).parent / 'assets'
output.mkdir(parents=True, exist_ok=True)

def decoded(name):
    return np.frombuffer(subprocess.check_output(['ffmpeg', '-loglevel', 'error', '-i', str(source / name), '-ac', '1', '-ar', '22050', '-f', 'f32le', '-']), dtype=np.float32).copy()

flyby = decoded('730351_bullet_fly_by_cc0.mp3')
crack = decoded('386854_bullet_crack_cc0.mp3')
highpass = np.ones(91) / 91
for i, moment in enumerate((1.45, 3.0, 5.2, 6.88, 9.48, 13.96)):
    for kind in ('far', 'near'):
        n = 13230
        initial = round((moment - 0.11) * 22050)
        x = flyby[max(0,initial):initial+n]
        x = np.pad(x, (0,max(0,n-len(x)))).astype(np.float64)
        x -= np.convolve(x, highpass, mode='same')
        x *= np.minimum(np.arange(n) / 180, 1)
        x *= np.minimum((n - np.arange(n)) / 2600, 1)
        if kind == 'near':
            click = np.pad(crack[:min(n,len(crack))],(0,max(0,n-len(crack)))).astype(np.float64)
            click -= np.convolve(click, highpass, mode='same')
            click *= np.exp(-np.arange(n)/4400)
            x = .75*x + .24*click
        peak = max(np.abs(x).max(), 1e-5)
        x = np.tanh(x / peak * 1.9) * .67
        pcm = np.int16(np.clip(x,-1,1) * 32767)
        filename = output / f'bullet_{kind}_{i+1}.wav'
        with wave.open(str(filename),'wb') as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(22050)
            f.writeframes(pcm.tobytes())
        print(filename.name, len(pcm)/22050,'seconds', filename.stat().st_size)
