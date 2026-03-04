import runpy
from pathlib import Path


if __name__ == "__main__":
    target = Path(__file__).resolve().parent / "tools" / "client" / "client_gui.py"
    runpy.run_path(str(target), run_name="__main__")
