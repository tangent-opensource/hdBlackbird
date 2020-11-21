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

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/defaults
                      ${CMAKE_SOURCE_DIR}/cmake/modules
                      ${CMAKE_SOURCE_DIR}/cmake/macros)

include(Public)
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

add_definitions(-DWITH_OPENSUBDIV)

add_definitions(
  ${BOOST_DEFINITIONS}
  ${OPENIMAGEIO_DEFINITIONS}
)

add_definitions(
  -DCCL_NAMESPACE_BEGIN=namespace\ ccl\ {
  -DCCL_NAMESPACE_END=}
)

if($ENV{USD_HOUDINI})
  option(USE_USD_HOUDINI "Use Houdini" ON)
  add_definitions(-DUSE_USD_HOUDINI)

  set(CMAKE_SHARED_LIBRARY_PREFIX "libpxr_")
  find_package(Houdini)

  set(HBoost_NO_SYSTEM_PATHS true)
  set(HBoost_USE_STATIC_LIBS OFF CACHE BOOL "use static libs")

  add_definitions(-DHBOOST_ALL_NO_LIB)
  add_definitions(-DHBOOST_ALL_DYN_LINK)
endif()

# -- External Packages

find_package(Python COMPONENTS Development Interpreter REQUIRED)
find_package(PyOpenGL REQUIRED)
find_package(PySide REQUIRED)


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

find_package(GLEW REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(OpenGL REQUIRED)
find_package(OpenJPEG REQUIRED)
find_package(TBB REQUIRED)
find_package(ZLIB REQUIRED)

find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(TIFF REQUIRED)
find_package(OpenImageIO REQUIRED)

find_package(OpenSubdiv)
find_package(OpenVDB)

find_package(Embree)
find_package(OpenColorIO)

find_package(USD REQUIRED)
find_package(Cycles REQUIRED)

# -- Build Flags

if("${USD_MINOR_VERSION}" STRGREATER_EQUAL "20")
add_definitions(-DUSD_HAS_NEW_SDR_NODE_CONSTRUCTOR)
endif()

if("${USD_MINOR_VERSION}" STRGREATER_EQUAL "20" AND 
   "${USD_PATCH_VERSION}" STRGREATER_EQUAL "8")
add_definitions(-DUSD_HAS_UDIM_RESOLVE_FIX)
endif()

# -- Source

add_subdirectory(plugin)
add_subdirectory(tools)
