import subprocess
import wave
from pathlib import Path

import numpy as np

root = Path(__file__).parent
raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(root / 'audio_sources/30932_aust_paul_ricochet_cc0.mp3'), '-ac', '1', '-ar', '22050', '-f', 'f32le', '-'])
audio = np.frombuffer(raw, dtype='<f4')
for index, start in enumerate((0.0, 0.50, 0.91, 1.60), 1):
    samples = audio[round(start * 22050):round((start + 0.36) * 22050)].astype(np.float64)
    samples = np.pad(samples, (0, max(0, 7938 - len(samples))))[:7938]
    samples -= np.convolve(samples, np.ones(61) / 61, mode='same')
    samples *= np.minimum(np.arange(len(samples)) / 100, 1)
    samples *= np.minimum((len(samples) - np.arange(len(samples))) / 4000, 1)
    samples *= .38 / max(.01, np.max(np.abs(samples)))
    output = root / 'assets' / f'ricochet_{index}.wav'
    with wave.open(str(output), 'wb') as dest:
        dest.setnchannels(1)
        dest.setsampwidth(2)
        dest.setframerate(22050)
        dest.writeframes((np.clip(samples, -1, 1) * 32767).astype('<i2').tobytes())
    print(output.name, output.stat().st_size)
