cmake_minimum_required(VERSION 3.22)

project(ESPPRC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
option(ESPPRC_ENABLE_OPENMP "Enable optional parallel labeling stages" ON)
if(ESPPRC_ENABLE_OPENMP)
  find_package(OpenMP QUIET)
endif()

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

# Always generate the wrapper from the maintained .pyx/.pxd files, so source
# distributions cannot silently compile a stale checked-in Cython translation.
set(ESPPRC_CYTHON_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/Bidirectional.cpp")
add_custom_command(
  OUTPUT "${ESPPRC_CYTHON_CPP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND "${Python3_EXECUTABLE}" -m cython --cplus -3
          --output-file "${ESPPRC_CYTHON_CPP}"
          "${CMAKE_CURRENT_SOURCE_DIR}/python/espprc/Bidirectional.pyx"
  DEPENDS python/espprc/Bidirectional.pyx python/espprc/Bidirectional.pxd
  VERBATIM
)
add_library(
  ESPPRC_Bidirectional MODULE
  "${ESPPRC_CYTHON_CPP}"
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

if(ESPPRC_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
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

install(FILES python/espprc/__init__.py python/espprc/datasets.py DESTINATION espprc)
install(DIRECTORY python/espprc/data DESTINATION espprc FILES_MATCHING PATTERN "*.json")
install(TARGETS ESPPRC_Bidirectional LIBRARY DESTINATION espprc RUNTIME DESTINATION espprc)

option(ESPPRC_BUILD_TESTS "Build native ESPPRC regression tests" OFF)
if(ESPPRC_BUILD_TESTS)
  enable_testing()
  add_executable(espprc_duration_bucket_tests
    test_duration_buckets.cc
    src/cc/search.cc
    src/cc/labelling.cc
    src/cc/ref_callback.cc
  )
  target_include_directories(espprc_duration_bucket_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cc")
  target_link_libraries(espprc_duration_bucket_tests PRIVATE
    LEMON::lemon fmt::fmt spdlog::spdlog)
  add_test(NAME duration_buckets COMMAND espprc_duration_bucket_tests)
  add_executable(espprc_bucket_dominance_tests
    test_bucket_dominance.cc
    src/cc/search.cc
    src/cc/labelling.cc
    src/cc/ref_callback.cc
  )
  target_include_directories(espprc_bucket_dominance_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cc")
  target_link_libraries(espprc_bucket_dominance_tests PRIVATE
    LEMON::lemon fmt::fmt spdlog::spdlog)
  add_test(NAME bucket_dominance COMMAND espprc_bucket_dominance_tests)
  add_executable(espprc_duplicate_index_tests
    test_duplicate_index.cc
    src/cc/search.cc
    src/cc/labelling.cc
    src/cc/ref_callback.cc
  )
  target_include_directories(espprc_duplicate_index_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cc")
  target_link_libraries(espprc_duplicate_index_tests PRIVATE
    LEMON::lemon fmt::fmt spdlog::spdlog)
  add_test(NAME duplicate_index COMMAND espprc_duplicate_index_tests)
  add_executable(espprc_bucket_extension_tests
    test_bucket_extension.cc
    src/cc/bidirectional.cc
    src/cc/digraph.cc
    src/cc/preprocessing.cc
    src/cc/search.cc
    src/cc/labelling.cc
    src/cc/ref_callback.cc
  )
  target_compile_definitions(espprc_bucket_extension_tests PRIVATE ESPPRC_TESTING)
  target_include_directories(espprc_bucket_extension_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cc")
  target_link_libraries(espprc_bucket_extension_tests PRIVATE
    LEMON::lemon fmt::fmt spdlog::spdlog)
  if(ESPPRC_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(espprc_bucket_extension_tests PRIVATE OpenMP::OpenMP_CXX)
  endif()
  add_test(NAME bucket_extension COMMAND espprc_bucket_extension_tests)
  add_executable(espprc_bucket_joining_tests
    test_bucket_joining.cc
    src/cc/bidirectional.cc
    src/cc/digraph.cc
    src/cc/labelling.cc
    src/cc/preprocessing.cc
    src/cc/ref_callback.cc
    src/cc/search.cc
  )
  target_compile_definitions(espprc_bucket_joining_tests PRIVATE ESPPRC_TESTING)
  target_include_directories(espprc_bucket_joining_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cc")
  target_link_libraries(espprc_bucket_joining_tests PRIVATE
    LEMON::lemon fmt::fmt spdlog::spdlog)
  if(ESPPRC_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(espprc_bucket_joining_tests PRIVATE OpenMP::OpenMP_CXX)
  endif()
  add_test(NAME bucket_joining COMMAND espprc_bucket_joining_tests)
endif()
