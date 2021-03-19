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

cmake_minimum_required(VERSION 3.12)

project(hdCycles CXX C)

find_package(Cycles REQUIRED)

add_library(UsdInterface INTERFACE)
add_library(Usd::Usd ALIAS UsdInterface)

add_definitions(-Wno-deprecated -DHD_CYCLES_VERSION="")

# two builds
if(DEFINED ENV{HFS})
    find_package(Houdini REQUIRED PATHS $ENV{HFS}/toolkit/cmake)
    target_link_libraries(UsdInterface
    INTERFACE
    Houdini
    )

    target_compile_definitions(UsdInterface
        INTERFACE
        BOOST_NS=hboost        
    )
else()
    # find usd cmake
endif()

if(WIN32)
  set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NODEFAULTLIB:libmmd.lib /NODEFAULTLIB:libirc.lib /NODEFAULTLIB:svml_dispmd.lib /NODEFAULTLIB:libdecimal.lib" )
  
  # Added for release debug symbols
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

set(CMAKE_CXX_STANDARD 14)

if("${USD_MINOR_VERSION}" STRGREATER_EQUAL "20" AND 
   "${USD_PATCH_VERSION}" STRGREATER_EQUAL "8")
add_definitions(-DUSD_HAS_UDIM_RESOLVE_FIX)
endif()

add_subdirectory(plugin)
add_subdirectory(tools)
