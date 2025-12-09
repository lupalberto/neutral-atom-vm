import os
from pathlib import Path

# Force headless Qt/Matplotlib so widget tests run without a display.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("MPLBACKEND", "Agg")

BUILD_PATH = Path(__file__).resolve().parent.parent / "build-editable"
os.environ.setdefault("SKBUILD_EDITABLE_SKIP", str(BUILD_PATH))
