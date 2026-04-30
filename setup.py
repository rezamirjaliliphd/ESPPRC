import sys
import platform
from setuptools import setup, Extension
from Cython.Build import cythonize

# Detect platform and compiler
is_windows = sys.platform.startswith('win')
is_macos = sys.platform == 'darwin'
is_linux = sys.platform.startswith('linux')

# Source files
sources = [
    "python/espprc/bidirectional_enhanced.pyx",
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

# Compiler and linker flags
extra_compile_args = []
extra_link_args = []

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
        "espprc.bidirectional_enhanced",
        sources=sources,
        include_dirs=include_dirs,
        libraries=libraries,
        library_dirs=library_dirs,
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
