from pathlib import Path
from importlib.metadata import version
from zipfile import ZipFile, BadZipFile
import hashlib
import os
import re
import subprocess
import sys
import urllib.request

root = Path("/content/DiffLocks-Studio")
python = Path("/content/facehair_env/bin/python")
launcher = Path("/content/difflocks_launcher.py")
task = root / "inference/assets/face_landmarker_v2_with_blendshapes.task"
source = root / "inference/img2hair.py"
weights = root / "checkpoints"

if not python.is_file() or not launcher.is_file():
    raise RuntimeError("Nie ma aktywnej instalacji DiffLocks. Otworz stary notebook w tej samej sesji Colab.")
if not list(weights.rglob("strand_codec.pt")) or not list(weights.rglob("*.pth")):
    raise RuntimeError("W tej sesji Colab nie znaleziono wag. Nie pobieram ich ponownie automatycznie.")

print("Face Hair - naprawa MediaPipe bez ponownego transferu 9 GB", flush=True)

installed = subprocess.run(
    [str(python), "-c", "from importlib.metadata import version; print(version('mediapipe'))"],
    capture_output=True, text=True, check=True
).stdout.strip()
print("MediaPipe zastane:", installed, flush=True)

if installed != "0.10.21":
    print("Instaluje zgodne MediaPipe 0.10.21 dla Python 3.12...", flush=True)
    subprocess.run(
        [str(python), "-m", "pip", "install", "--no-input", "--disable-pip-version-check",
         "mediapipe==0.10.21"],
        check=True
    )

required = {"face_detector.tflite", "face_landmarks_detector.tflite",
            "face_blendshapes.tflite", "geometry_pipeline_metadata_landmarks.binarypb"}


def valid_model(file):
    if not file.exists() or file.stat().st_size < 1_000_000:
        return False
    try:
        with ZipFile(file) as z:
            return required.issubset(z.namelist()) and z.testzip() is None
    except (BadZipFile, OSError):
        return False


if not valid_model(task):
    print("Naprawiam brakujacy/uszkodzony model punktow twarzy (3.8 MB)...", flush=True)
    task.parent.mkdir(parents=True, exist_ok=True)
    temporary = task.with_suffix(".download")
    urllib.request.urlretrieve(
        "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task",
        temporary
    )
    if not valid_model(temporary):
        raise RuntimeError("Model pobrany z Google jest uszkodzony.")
    temporary.replace(task)

print("Model punktow twarzy poprawny:", task.stat().st_size, "bajtow", flush=True)
original = source.read_text()
old = "asset_path = 'inference/assets/face_landmarker_v2_with_blendshapes.task'"
new = "asset_path = os.path.join(os.path.dirname(__file__), 'assets/face_landmarker_v2_with_blendshapes.task')"
if old in original:
    source.write_text(original.replace(old, new, 1))
    print("Naprawiono sciezke modelu MediaPipe.", flush=True)

check_code = """
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
p = "/content/DiffLocks-Studio/inference/assets/face_landmarker_v2_with_blendshapes.task"
options = vision.FaceLandmarkerOptions(
    base_options=python.BaseOptions(model_asset_path=p),
    output_face_blendshapes=True,
    output_facial_transformation_matrixes=True,
    num_faces=1
)
detector=vision.FaceLandmarker.create_from_options(options)
print("MediaPipe FaceLandmarker OK",flush=True)
detector.close()
"""
subprocess.run([str(python), "-c", check_code], check=True)
# The Studio fork often contains Git LFS pointers instead of real PLY meshes.
# Fetch the original upstream meshes and verify their exact SHA256 values.
meshes = {
    "scalp.ply": ("5f111210f98d5785ee7458cd87314987ec8f9416c4bfc7944fe114c1fa76883c", 29027),
    "smplx_base.ply": ("09d708271ecb602e24dc4817bcbea7d8f0d3be6a4dd9af79f5d94583cef33525", 679254),
}
body_dir = root / "data_loader/difflocks_bodydata"
body_dir.mkdir(parents=True, exist_ok=True)
for filename, (digest, size) in meshes.items():
    file = body_dir / filename
    if file.is_file() and file.stat().st_size == size and hashlib.sha256(file.read_bytes()).hexdigest() == digest:
        print(f"{filename}: prawidlowy ({size} bajtow).", flush=True)
        continue
    print(f"{filename}: naprawiam plik Git LFS ze zrodla projektu...", flush=True)
    url = "https://raw.githubusercontent.com/Meshcapade/difflocks/main/data_loader/difflocks_bodydata/" + filename
    temp = file.with_suffix(".fixed")
    urllib.request.urlretrieve(url, temp)
    raw = temp.read_bytes()
    if len(raw) != size or not raw.startswith(b"ply\n") or hashlib.sha256(raw).hexdigest() != digest:
        temp.unlink(missing_ok=True)
        raise RuntimeError(f"Niepoprawny plik PLY: {filename}")
    temp.replace(file)
    print(f"{filename}: poprawny PLY - {len(raw)} bajtow.", flush=True)

print("MediaPipe działa. Uruchamiam DiffLocks...", flush=True)

launch_text = launcher.read_text()
old_call = '    str(ckpt_files[0]),\n    DEVICE\n)'
new_call = '    str(ckpt_files[0]),\n    str(REPO_DIR / "checkpoints/rgb2material/rgb2material.pt")\n)'
if old_call in launch_text:
    launcher.write_text(launch_text.replace(old_call, new_call, 1))
    print("Poprawiono argument modelu materialow.",flush=True)

env=os.environ.copy()
env["PATH"]="/content/facehair_env/bin:"+env.get("PATH","")
env["PYTHONNOUSERSITE"]="1"
env.pop("PYTHONPATH",None)
proc=subprocess.Popen(
    [str(python), "-u", str(launcher)],
    cwd=str(root),
    env=env,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1
)
for line in proc.stdout:
    print(line,end="",flush=True)
    found=re.search(r"https://[a-zA-Z0-9-]+\.gradio\.live",line)
    if found:
        print("OTWORZ GENERATOR:",found.group(0),flush=True)
status=proc.wait()
if status:
    raise RuntimeError(f"DiffLocks zakonczyl sie kodem {status}. Szczegoly powyzej.")
