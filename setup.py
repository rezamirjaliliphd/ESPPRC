import sys
import platform
import os
from setuptools import setup, Extension
from Cython.Build import cythonize

# Detect platform and compiler
is_windows = sys.platform.startswith('win')
is_macos = sys.platform == 'darwin'
is_linux = sys.platform.startswith('linux')

# Source files
sources = [
    "python/espprc/Bidirectional.pyx",
    "src/cc/bidirectional.cc",
    "src/cc/digraph.cc",
    "src/cc/labelling.cc",
    "src/cc/preprocessing.cc",
    "src/cc/ref_callback.cc",
    "src/cc/search.cc"
]

# Include directories
include_dirs = [".", "src/cc"]

# Libraries
libraries = ["lemon", "fmt"]
library_dirs = []
runtime_library_dirs = []

# Compiler and linker flags
extra_compile_args = []
extra_link_args = []


def _append_existing(paths, target):
    for path in paths:
        if path and os.path.isdir(path) and path not in target:
            target.append(path)


def _append_env_paths(env_var, target):
    raw_value = os.environ.get(env_var, "")
    _append_existing(raw_value.split(os.pathsep), target)


# Prefer dependencies installed into the active conda environment.
conda_prefix = os.environ.get("CONDA_PREFIX") or os.environ.get("PREFIX")
if conda_prefix:
    if is_windows:
        _append_existing(
            [
                os.path.join(conda_prefix, "Library", "include"),
                os.path.join(conda_prefix, "include"),
            ],
            include_dirs,
        )
        _append_existing(
            [
                os.path.join(conda_prefix, "Library", "lib"),
                os.path.join(conda_prefix, "libs"),
                os.path.join(conda_prefix, "lib"),
            ],
            library_dirs,
        )
    else:
        _append_existing(
            [
                os.path.join(conda_prefix, "include"),
            ],
            include_dirs,
        )
        conda_lib_dirs = [
            os.path.join(conda_prefix, "lib"),
            os.path.join(conda_prefix, "lib64"),
        ]
        _append_existing(conda_lib_dirs, library_dirs)
        _append_existing(conda_lib_dirs, runtime_library_dirs)

_append_env_paths("ESPPRC_INCLUDE_DIRS", include_dirs)
_append_env_paths("ESPPRC_LIBRARY_DIRS", library_dirs)
_append_env_paths("ESPPRC_RUNTIME_LIBRARY_DIRS", runtime_library_dirs)

if is_windows:
    # MSVC on Windows
    extra_compile_args = [
        "/std:c++17", 
        "/O2", 
        "/EHsc", 
        "/utf-8", 
        "/bigobj"
    ]
    extra_link_args = []
    
    # vcpkg paths (if available)
    include_dirs.append("C:/vcpkg/installed/x64-windows/include")
    library_dirs.extend([
        "C:/vcpkg/installed/x64-windows/lib",
        "C:/vcpkg/installed/x64-windows/debug/lib"
    ])
    
elif is_macos:
    # Clang/GCC on macOS
    extra_compile_args = [
        "-std=c++17",
        "-O3",
        "-Wno-unused-function",
        "-Wno-uninitialized"
    ]
    
    # Homebrew paths
    include_dirs.extend([
        "/usr/local/include",
        "/opt/homebrew/include"
    ])
    library_dirs.extend([
        "/usr/local/lib",
        "/opt/homebrew/lib"
    ])
    
else:  # Linux
    # GCC/Clang on Linux
    extra_compile_args = [
        "-std=c++17",
        "-O3",
        "-Wno-unused-function",
        "-Wno-uninitialized"
    ]
    extra_link_args = []
    
    # Standard Linux library paths
    library_dirs.extend([
        "/usr/lib",
        "/usr/local/lib"
    ])

# Create extension
ext_modules = [
    Extension(
        "espprc.Bidirectional",
        sources=sources,
        include_dirs=include_dirs,
        libraries=libraries,
        library_dirs=library_dirs,
        runtime_library_dirs=runtime_library_dirs if not is_windows else [],
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language="c++"
    )
]

# Build
setup(
    ext_modules=cythonize(
        ext_modules,
        compiler_directives={
            'language_level': "3",
            'boundscheck': False,
            'wraparound': False,
            'initializedcheck': False,
            'cdivision': True
        }
    )
)
