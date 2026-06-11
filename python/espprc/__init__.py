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

__all__ = []

# Import the compiled extension explicitly
try:
    from . import Bidirectional

    # re-export module
    __all__.append("Bidirectional")

    if hasattr(Bidirectional, "solve_espprc"):
        solve_espprc = Bidirectional.solve_espprc
        solve_espprc_enhanced = solve_espprc
        ESPPRC = solve_espprc
        __all__.append("solve_espprc")
        __all__.append("solve_espprc_enhanced")
        __all__.append("ESPPRC")

except Exception as e:
    # Keep package importable even if extension fails
    # (DO NOT import fallback Python modules here unless really needed)
    pass
