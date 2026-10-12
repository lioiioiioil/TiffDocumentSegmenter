"""Prepare the filenames expected by the original TIFF segmenter."""
import argparse
from pathlib import Path
import shutil
import subprocess
import os

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--input", type=Path, default=root/"examples/document.tif")
p.add_argument("--binary", type=Path)
a = p.parse_args()
binary = a.binary.resolve() if a.binary else root/"build"/("segmenter.exe" if os.name == "nt" else "segmenter")
if not binary.is_file():
    p.error(f"Build the project first; executable not found: {binary}")
if not a.input.is_file():
    p.error(f"Input does not exist: {a.input}")
run = root/"runs/demo"
(run/"results").mkdir(parents=True, exist_ok=True)
shutil.copy2(a.input, run/"17.tif")
subprocess.run([str(binary)], cwd=run, check=True)
print(f"Results: {run/'results'}")
