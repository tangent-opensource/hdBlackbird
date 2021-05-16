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

cmake_minimum_required(VERSION 3.18)
project(hdCycles CXX C)

# Options
option(USE_HOUDINI_USD "" OFF)
option(USE_CXX11_ABI "" ON)

# Options debug
option(USE_TESTS "" OFF)
option(USE_ASAN "" OFF)

# Global Settings
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if (MSVC)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NODEFAULTLIB:libmmd.lib /NODEFAULTLIB:libirc.lib /NODEFAULTLIB:svml_dispmd.lib /NODEFAULTLIB:libdecimal.lib" )
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

if(NOT MSVC AND DEFINED USE_CXX11_ABI)
    add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
endif()

if(NOT DEFINED HD_CYCLES_VERSION)
    add_compile_definitions(HD_CYCLES_VERSION="0.0.0")
else()
    add_compile_definitions(HD_CYCLES_VERSION="${HD_CYCLES_VERSION}")
endif()

# Find Cycles and Usd
find_package(Cycles REQUIRED)
include(cmake/FindUsd.cmake)

# Linux only
if(${USE_TESTS} AND ${USE_ASAN})
    add_compile_definitions(USE_ASAN)
    add_compile_options(-fno-omit-frame-pointer -fsanitize=address)
    add_link_options(-fno-omit-frame-pointer -fsanitize=address)
endif()

add_subdirectory(plugin)

if(${USE_TESTS})
    message(STATUS "Building with tests")
    add_subdirectory(tests)
endif()
