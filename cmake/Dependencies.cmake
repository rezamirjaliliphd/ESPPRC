include(FetchContent)

option(ESPPRC_VENDOR_DEPS "Download missing C++ dependencies with FetchContent" ON)

foreach(prefix_var CONDA_PREFIX PREFIX)
  if(DEFINED ENV{${prefix_var}} AND NOT "$ENV{${prefix_var}}" STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{${prefix_var}}")
  endif()
endforeach()

find_package(fmt CONFIG QUIET)
if(NOT fmt_FOUND)
  if(NOT ESPPRC_VENDOR_DEPS)
    message(FATAL_ERROR "fmt was not found. Install fmt or enable ESPPRC_VENDOR_DEPS.")
  endif()

  set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
  )
  FetchContent_MakeAvailable(fmt)
endif()

find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
  if(NOT ESPPRC_VENDOR_DEPS)
    message(FATAL_ERROR "spdlog was not found. Install spdlog or enable ESPPRC_VENDOR_DEPS.")
  endif()

  set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
  set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
  )
  FetchContent_MakeAvailable(spdlog)
endif()

find_path(
  LEMON_INCLUDE_DIR
  NAMES lemon/smart_graph.h
  HINTS
    "$ENV{ESPPRC_INCLUDE_DIRS}"
    "$ENV{CONDA_PREFIX}"
    "$ENV{PREFIX}"
    "$ENV{VCPKG_ROOT}/installed/x64-windows"
  PATH_SUFFIXES include Library/include
)
find_library(
  LEMON_LIBRARY
  NAMES lemon
  HINTS
    "$ENV{ESPPRC_LIBRARY_DIRS}"
    "$ENV{CONDA_PREFIX}"
    "$ENV{PREFIX}"
    "$ENV{VCPKG_ROOT}/installed/x64-windows"
  PATH_SUFFIXES lib lib64 Library/lib
)

if(LEMON_INCLUDE_DIR AND LEMON_LIBRARY)
  add_library(LEMON::lemon UNKNOWN IMPORTED)
  set_target_properties(
    LEMON::lemon
    PROPERTIES
      IMPORTED_LOCATION "${LEMON_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LEMON_INCLUDE_DIR}"
  )
elseif(ESPPRC_VENDOR_DEPS)
  FetchContent_Declare(
    lemon
    URL https://lemon.cs.elte.hu/pub/sources/lemon-1.3.1.tar.gz
  )
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
  FetchContent_GetProperties(lemon)
  if(NOT lemon_POPULATED)
    FetchContent_Populate(lemon)
    add_subdirectory("${lemon_SOURCE_DIR}" "${lemon_BINARY_DIR}" EXCLUDE_FROM_ALL)
  endif()

  if(TARGET lemon)
    target_include_directories(
      lemon
      PUBLIC
        "${lemon_SOURCE_DIR}"
        "${lemon_BINARY_DIR}"
    )
    add_library(LEMON::lemon ALIAS lemon)
  else()
    message(FATAL_ERROR "LEMON was downloaded, but its CMake target was not named 'lemon'.")
  endif()
else()
  message(FATAL_ERROR "LEMON was not found. Install lemon or enable ESPPRC_VENDOR_DEPS.")
endif()
