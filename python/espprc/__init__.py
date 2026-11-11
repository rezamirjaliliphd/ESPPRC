# Ensure dependent DLLs (lemon.dll, fmt.dll) are discoverable on Windows
import os, sys

def _add_vcpkg_bin():
    # Try common spots; adjust if your path differs
    conda_prefix = os.environ.get("CONDA_PREFIX") or os.environ.get("PREFIX")
    candidates = [
        conda_prefix and os.path.join(conda_prefix, "Library", "bin"),
        os.environ.get("VCPKG_ROOT") and os.path.join(os.environ["VCPKG_ROOT"], "installed", "x64-windows", "bin"),
        r"C:\vcpkg\installed\x64-windows\bin",
    ]
    for p in candidates:
        if p and os.path.isdir(p):
            try:
                # Python 3.8+ explicit DLL search path
                os.add_dll_directory(p)
            except Exception:
                # Fallback for older Pythons
                os.environ["PATH"] = p + os.pathsep + os.environ.get("PATH", "")
            break

if sys.platform == "win32":
    _add_vcpkg_bin()

__version__ = "0.3.0.dev0"
from . import Bidirectional
from .Bidirectional import solve_espprc

solve_espprc_enhanced = solve_espprc
ESPPRC = solve_espprc
__all__ = ["Bidirectional", "solve_espprc", "solve_espprc_enhanced", "ESPPRC"]
