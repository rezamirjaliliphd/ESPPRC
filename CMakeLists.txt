cmake_minimum_required(VERSION 3.22)

project(ESPPRC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(OpenMP QUIET)

message(STATUS "ESPPRC target platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "ESPPRC C++ compiler: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER})")

if(WIN32 AND MSVC)
  message(STATUS "ESPPRC build mode: Windows with Microsoft Visual C++")
elseif(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  message(STATUS "ESPPRC build mode: Windows with GNU/g++")
elseif(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  message(STATUS "ESPPRC build mode: Linux with GNU/g++")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(STATUS "ESPPRC build mode: Clang/LLVM")
else()
  message(STATUS "ESPPRC build mode: generic C++ compiler")
endif()

if(NOT DEFINED Python3_EXTENSION_SUFFIX OR Python3_EXTENSION_SUFFIX STREQUAL "")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX') or sysconfig.get_config_var('SO') or '.so')"
    OUTPUT_VARIABLE Python3_EXTENSION_SUFFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

include(Dependencies)

add_library(
  ESPPRC_Bidirectional MODULE
  python/espprc/Bidirectional.cpp
  src/cc/bidirectional.cc
  src/cc/digraph.cc
  src/cc/labelling.cc
  src/cc/preprocessing.cc
  src/cc/ref_callback.cc
  src/cc/search.cc
)

target_include_directories(
  ESPPRC_Bidirectional
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/espprc"
)

target_link_libraries(
  ESPPRC_Bidirectional
  PRIVATE
    Python3::Module
    LEMON::lemon
    fmt::fmt
    spdlog::spdlog
)

if(OpenMP_CXX_FOUND)
  target_link_libraries(ESPPRC_Bidirectional PRIVATE OpenMP::OpenMP_CXX)
  message(STATUS "ESPPRC OpenMP: enabled")
else()
  message(STATUS "ESPPRC OpenMP: not found; building without OpenMP")
endif()

if(MSVC)
  target_compile_options(ESPPRC_Bidirectional PRIVATE /O2 /EHsc /utf-8 /bigobj)
else()
  target_compile_options(
    ESPPRC_Bidirectional
    PRIVATE
      -O3
      -Wno-unused-function
      -Wno-uninitialized
  )
endif()

set_target_properties(
  ESPPRC_Bidirectional
  PROPERTIES
    PREFIX ""
    OUTPUT_NAME "Bidirectional"
    SUFFIX "${Python3_EXTENSION_SUFFIX}"
)

install(FILES python/espprc/__init__.py DESTINATION espprc)
install(TARGETS ESPPRC_Bidirectional LIBRARY DESTINATION espprc RUNTIME DESTINATION espprc)
