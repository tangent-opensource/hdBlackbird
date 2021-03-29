#  Copyright 2020 Tangent Animation
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied,
#  including without limitation, as related to merchantability and fitness
#  for a particular purpose.
#
#  In no event shall any copyright holder be liable for any damages of any kind
#  arising from the use of this software, whether in contract, tort or otherwise.
#  See the License for the specific language governing permissions and
#  limitations under the License.

project(hdCycles)

cmake_minimum_required(VERSION 3.12)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
                      ${CMAKE_SOURCE_DIR}/cmake/defaults
                      ${CMAKE_SOURCE_DIR}/cmake/modules
                      ${CMAKE_SOURCE_DIR}/cmake/macros)

include(Options)
include(ProjectDefaults)

# CXXDefaults will set a variety of variables for the project.
# Consume them here. This is an effort to keep the most common
# build files readable.
include(CXXDefaults)

add_definitions(${_PXR_CXX_DEFINITIONS})
set(CMAKE_CXX_FLAGS "${_PXR_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")

if(WIN32)
  set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NODEFAULTLIB:libmmd.lib /NODEFAULTLIB:libirc.lib /NODEFAULTLIB:svml_dispmd.lib /NODEFAULTLIB:libdecimal.lib" )
  
  # Added for release debug symbols
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

set(CMAKE_CXX_STANDARD 14)

# -- HdCycles Definitions

add_definitions(
  ${BOOST_DEFINITIONS}
  ${OPENIMAGEIO_DEFINITIONS}
)

add_definitions(
  -DCCL_NAMESPACE_BEGIN=namespace\ ccl\ {
  -DCCL_NAMESPACE_END=}
)

# Default variant USD Pixar
set(USD_BUILD_VARIANT "USD_PIXAR")

if(DEFINED ENV{USD_BUILD_VARIANT})
  file(TO_CMAKE_PATH $ENV{USD_BUILD_VARIANT} USD_BUILD_VARIANT)
endif()

# -- External Packages

# - Python

# TODO: Temp fix due to python2 linux issues
if(DEFINED ENV{REZ_PYTHON_BASE})
  if (UNIX)
    set(PYTHON_LIBRARY "$ENV{REZ_PYTHON_ROOT}/lib")
  endif()
endif()

find_package(Python COMPONENTS Development Interpreter REQUIRED)

# TODO: Temp fix due to python2 linux issues
if(DEFINED ENV{REZ_PYTHON_BASE})
  if (UNIX)
    set(PYTHON_INCLUDE_DIR "$ENV{REZ_PYTHON_ROOT}/include/python2.7")
  endif()
endif()

# - Boost

file(TO_CMAKE_PATH $ENV{BOOST_INCLUDE_DIR} BOOST_INCLUDEDIR)

# Boost python libraries in some cases do not have a trailing version.
# Try the versioned library first and fall back to a non-versioned library.

set(boost_python_component python${Python_VERSION_MAJOR}${Python_VERSION_MINOR})
string(TOUPPER ${boost_python_component} boost_python_component_upper)

find_package(Boost COMPONENTS ${boost_python_component} thread program_options filesystem) # Not required

if(NOT ${Boost_${boost_python_component_upper}_FOUND})
  # We need to unset this or the new find_package won't overwrite it.
  unset(Boost_LIBRARIES)
  find_package(Boost COMPONENTS python thread program_options filesystem REQUIRED)
endif()

add_definitions(-DBOOST_NS=boost)

# -- Houdini Deps

# - Legacy Houdini find path

find_package(Houdini)
if(${Houdini_FOUND})
  # This code path is only used by third party building, our internal builds
  # set USD_BUILD_VARIANT. It's likely that this path will be removed...
  set(USD_BUILD_VARIANT "USD_HOUDINI")

  add_definitions(-DUSE_LEGACY_HOUDINI)
endif()

# - Proper USD_HOUDINI CMake/rez path

if(${USD_BUILD_VARIANT} STREQUAL "USD_HOUDINI")
  # HBoost has been manually extracted and rezified to allow this Find command
  find_package(HBoost)

  set(CMAKE_SHARED_LIBRARY_PREFIX "libpxr_")

  set(HBoost_NO_SYSTEM_PATHS true)
  set(HBoost_USE_STATIC_LIBS OFF CACHE BOOL "use static libs")

  add_definitions(-DHBOOST_ALL_NO_LIB)
  add_definitions(-DHBOOST_ALL_DYN_LINK)

  # We set these via rez, but are useful to expose here
  add_definitions(-DUSE_HBOOST)
  add_definitions(-DBOOST_NS=hboost)
endif()

# -- External Deps

find_package(OpenGL)

if (${OpenGL_FOUND})
  find_package(GLEW REQUIRED)
  find_package(PyOpenGL)
  find_package(PySide)
  set(USE_OPENGL TRUE)
  message("[HDCYCLES] Configured with OpenGL")
endif()

find_package(OpenEXR REQUIRED)
find_package(TBB REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenImageIO REQUIRED)
find_package(OpenColorIO)

# TODO: These are unfornate. We should move these elsewhere...
file(TO_CMAKE_PATH $ENV{LIBJPEG_TURBO_LIBRARY_DIR} OPENJPEG_LIBRARY)
file(TO_CMAKE_PATH $ENV{LIBJPEG_TURBO_INCLUDE_DIR} OPENJPEG_INCLUDE_DIR)
file(TO_CMAKE_PATH $ENV{LIBJPEG_TURBO_ROOT} JPEGTURBO_PATH)

find_package(OpenJPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(TIFF REQUIRED)

find_package(OpenSubdiv)
if(${OpenSubdiv_FOUND})
  add_definitions(-DWITH_OPENSUBDIV)
endif()

find_package(OpenVDB)
if(${OpenVDB_FOUND})
  set(WITH_OPENVDB ON)
  set(OPENVDB_DEFINITIONS -DNOMINMAX -D_USE_MATH_DEFINES)
  add_definitions(-DWITH_OPENVDB ${OPENVDB_DEFINITIONS})
  message("[HDCYCLES] Configured with OpenVDB")
endif()

find_package(Embree)
if(${Embree_FOUND})
  set(WITH_EMBREE ON)
  add_definitions(-DWITH_EMBREE)
  add_definitions(-DEMBREE_STATIC_LIB)
  message("[HDCYCLES] Configured with Embree")
endif()

find_package(USD REQUIRED)
find_package(Cycles REQUIRED)

# - UsdCycles Schema

find_package(UsdCycles REQUIRED)
if(${UsdCycles_FOUND})
  option(USE_USD_CYCLES_SCHEMA "Use UsdCycles Schema" ON)
  add_definitions(-DUSE_USD_CYCLES_SCHEMA)
endif()

# - Cycles Logging

find_package(Glog)
if(${Glog_FOUND})
  add_definitions(${GLOG_DEFINES})
endif()

find_package(Gflags)
if(${Gflags_FOUND})
  add_definitions(-DCYCLES_GFLAGS_NAMESPACE=${GFLAGS_NAMESPACE})
endif()

if(${Glog_FOUND} AND ${Gflags_FOUND})
  add_definitions(-DWITH_CYCLES_LOGGING)
endif()

# -- Build Flags

if("${USD_MINOR_VERSION}" STRGREATER_EQUAL "20" AND 
   "${USD_PATCH_VERSION}" STRGREATER_EQUAL "8")
add_definitions(-DUSD_HAS_UDIM_RESOLVE_FIX)
endif()

# -- Source

add_subdirectory(plugin)
add_subdirectory(tools)
