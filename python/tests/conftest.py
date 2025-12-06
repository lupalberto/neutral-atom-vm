import os
from pathlib import Path

BUILD_PATH = Path(__file__).resolve().parent.parent / "build-editable"
os.environ.setdefault("SKBUILD_EDITABLE_SKIP", str(BUILD_PATH))
