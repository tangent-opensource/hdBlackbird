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

if(UNIX)
    set(CYCLES_LIB_PREFIX lib)
endif()

# Cycles Includes

find_path(CYCLES_INCLUDE_DIRS "render/graph.h"
    HINTS
        ${CYCLES_ROOT}/include
        $ENV{CYCLES_ROOT}/include
        ${CYCLES_ROOT}/src
        $ENV{CYCLES_ROOT}/src
    DOC "Cycles Include directory")

# Cycles Libraries

find_path(CYCLES_LIBRARY_DIR
    NAMES
    ${CYCLES_LIB_PREFIX}cycles_bvh${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}cycles_device${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}cycles_graph${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}cycles_kernel${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}cycles_render${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}cycles_subd${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}cycles_util${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}extern_clew${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}extern_cuew${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${CYCLES_LIB_PREFIX}extern_numaapi${CMAKE_STATIC_LIBRARY_SUFFIX}

    HINTS
    ${CYCLES_ROOT}/lib
    $ENV{CYCLES_ROOT}/lib

    DOC "Cycles Libraries directory")

set(CYCLES_LIBS cycles_bvh;cycles_device;cycles_graph;cycles_kernel;cycles_render;cycles_subd;cycles_util;extern_clew;extern_cuew;extern_numaapi)

foreach (lib ${CYCLES_LIBS})
    find_library(${lib}_LIBRARY
        NAMES ${CYCLES_LIB_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}
        HINTS ${CYCLES_LIBRARY_DIR})
    if (${lib}_LIBRARY)
        add_library(${lib} INTERFACE IMPORTED)
        set_target_properties(${lib}
            PROPERTIES
            INTERFACE_LINK_LIBRARIES ${${lib}_LIBRARY}
        )
        list(APPEND CYCLES_LIBRARIES ${${lib}_LIBRARY})
    endif ()
endforeach ()

# Version Parsing

if(CYCLES_INCLUDE_DIRS AND EXISTS "${CYCLES_INCLUDE_DIRS}/pxr/pxr.h")
    foreach(_cycles_comp MAJOR MINOR PATCH)
        file(STRINGS
            "${CYCLES_INCLUDE_DIRS}/util/util_version.h"
            _cycles_tmp
            REGEX "#define CYCLES_VERSION_${_cycles_comp} .*$")
        string(REGEX MATCHALL "[0-9]+" CYCLES_VERSION_${_cycles_comp} ${_cycles_tmp})
    endforeach()
    set(CYCLES_VERSION ${CYCLES_MAJOR_VERSION}.${CYCLES_MINOR_VERSION}.${CYCLES_PATCH_VERSION})
endif()

find_path(ATOMIC_INCLUDE_DIRS "atomic_ops.h"
    HINTS
    ${CYCLES_ROOT}/third_party/atomic
    $ENV{CYCLES_ROOT}/third_party/atomic
    DOC "Atomic Include directory")

list(APPEND CYCLES_INCLUDE_DIRS ${ATOMIC_INCLUDE_DIRS})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Cycles
    REQUIRED_VARS
        CYCLES_INCLUDE_DIRS
        CYCLES_LIBRARY_DIR
        CYCLES_LIBRARIES
    VERSION_VAR
        CYCLES_VERSION)