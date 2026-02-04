# Ensure dependent DLLs (lemon.dll, fmt.dll) are discoverable on Windows
import os, sys

def _add_vcpkg_bin():
    # Try common spots; adjust if your path differs
    candidates = [
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
    from . import bidirectional_enhanced

    # re-export module
    __all__.append("bidirectional_enhanced")

    # re-export main entry point at top level (optional but recommended)
    if hasattr(bidirectional_enhanced, "solve_espprc_enhanced"):
        solve_espprc_enhanced = bidirectional_enhanced.solve_espprc_enhanced
        __all__.append("solve_espprc_enhanced")

except Exception as e:
    # Keep package importable even if extension fails
    # (DO NOT import fallback Python modules here unless really needed)
    pass
