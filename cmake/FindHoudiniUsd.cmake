#  Copyright 2021 Tangent Animation
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

if(NOT DEFINED HOUDINI_ROOT)
    message(FATAL_ERROR "HOUDINI_ROOT not defined")
endif()

find_package(Houdini REQUIRED PATHS ${HOUDINI_ROOT}/toolkit/cmake)
target_link_libraries(UsdInterface INTERFACE Houdini)

# DSO
target_compile_definitions(UsdInterface
    INTERFACE
    USE_HBOOST
    BOOST_NS=hboost
)

set(_houdini_libs OpenImageIO_sidefx;hboost_filesystem-mt-x64;hboost_iostreams-mt-x64;hboost_system-mt-x64;hboost_regex-mt-x64)
foreach(_houdini_lib ${_houdini_libs})
    find_library(${_houdini_lib}_path
            NAMES
            ${_houdini_lib}
            PATHS
            ${HOUDINI_ROOT}/dsolib
            ${HOUDINI_ROOT}/custom/houdini/dsolib/
            ${HOUDINI_ROOT}/Libraries
            REQUIRED
            )

    message(STATUS "Found ${_houdini_lib}: ${${_houdini_lib}_path}")

    target_link_libraries(UsdInterface
            INTERFACE
            ${${_houdini_lib}_path}
            )

endforeach()

# Python
find_library(_houdini_python_lib
    NAMES
    python27
    python2.7
    python
    PATHS
    ${HOUDINI_ROOT}/python27/libs
    ${HOUDINI_ROOT}/python/libs
    ${HOUDINI_ROOT}/python/lib
    REQUIRED
    )

find_library(_houdini_hboost_python
        NAMES
        hboost_python27-mt-x64
        hboost_python-mt-x64
        PATHS
        ${HOUDINI_ROOT}/dsolib
        ${HOUDINI_ROOT}/custom/houdini/dsolib/
        ${HOUDINI_ROOT}/Libraries
        REQUIRED
        )

target_link_libraries(Houdini INTERFACE ${_houdini_python_lib} ${_houdini_hboost_python})

# Usd
list(APPEND CMAKE_FIND_LIBRARY_PREFIXES lib) # append lib prefix to have same behaviour on win and lin
set(_houdini_pxr_libs pxr_ar;pxr_arch;pxr_cameraUtil;pxr_garch;pxr_gf;pxr_glf;pxr_hd;pxr_hdSt;pxr_hdx;pxr_hf;pxr_hgi;pxr_hgiGL;pxr_hgiInterop;pxr_hio;pxr_js;pxr_kind;pxr_ndr;pxr_pcp;pxr_plug;pxr_pxOsd;pxr_sdf;pxr_sdr;pxr_tf;pxr_trace;pxr_usd;pxr_usdAppUtils;pxr_usdGeom;pxr_usdHydra;pxr_usdImaging;pxr_usdImagingGL;pxr_usdLux;pxr_usdMedia;pxr_usdRender;pxr_usdRi;pxr_usdRiImaging;pxr_usdShade;pxr_usdSkel;pxr_usdSkelImaging;pxr_usdUI;pxr_usdUtils;pxr_usdviewq;pxr_usdVol;pxr_usdVolImaging;pxr_vt;pxr_work;)
foreach(_pxr_lib ${_houdini_pxr_libs})
    find_library(${_pxr_lib}_path
            NAMES
            ${_pxr_lib}
            PATHS
            ${HOUDINI_ROOT}/dsolib
            ${HOUDINI_ROOT}/custom/houdini/dsolib/
            ${HOUDINI_ROOT}/Libraries
            REQUIRED
            )

    target_link_libraries(UsdInterface
            INTERFACE
            ${${_pxr_lib}_path}
            )

endforeach()

# Find Usd Schema Generator

if(APPLE)
        set(HOUDINI_BIN ${HOUDINI_ROOT}/Resources/bin)
else()
        set(HOUDINI_BIN ${HOUDINI_ROOT}/bin)
endif()

find_program(USD_SCHEMA_GENERATOR
        NAMES
        usdGenSchema.py
        usdGenSchema
        PATHS
        ${HOUDINI_BIN}
        REQUIRED
        NO_DEFAULT_PATH
        )
list(PREPEND USD_SCHEMA_GENERATOR ${HOUDINI_BIN}/hython)
set(USD_SCHEMA_GENERATOR ${USD_SCHEMA_GENERATOR} CACHE STRING "" FORCE)
