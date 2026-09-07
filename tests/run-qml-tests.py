#!/usr/bin/env python3
"""Run the real QML pages with fake services, without starting the music app."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="tidal-wave-qml-") as tmp:
    module = Path(tmp) / "TidalWave"
    shutil.copytree(root / "qml", module / "qml")
    entries = ["module TidalWave"]
    for path in sorted((module / "qml").rglob("*.qml")):
        singleton = "singleton " if "pragma Singleton" in path.read_text() else ""
        entries.append(f"{singleton}{path.stem} 1.0 {path.relative_to(module)}")
    (module / "qmldir").write_text("\n".join(entries) + "\n")
    result = subprocess.run(
        ["qmltestrunner", "-import", tmp, "-input", str(root / "tests/qml")],
        env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software"},
    )
    raise SystemExit(result.returncode)
