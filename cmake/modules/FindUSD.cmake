# Copyright 2019 Luma Pictures
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# TODO: We might want to upgrade this FindUSD.cmake

# Simple module to find USD.

if (WIN32)
    # On Windows we need to find ".lib"... which is CMAKE_STATIC_LIBRARY_SUFFIX
    # on WIN32 (CMAKE_SHARED_LIBRARY_SUFFIX is ".dll")
    set(USD_LIB_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX}
        CACHE STRING "Extension of USD libraries")
else ()
    # Defaults to ".so" on Linux, ".dylib" on MacOS
    set(USD_LIB_SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
        CACHE STRING "Extension of USD libraries")
endif ()

# Note: for USD <= 0.19.11, there was a bug where, regardless of what
# PXR_LIB_PREFIX was set to, the behavior on windows was that the .lib files
# ALWAYS had no prefix.  However, the PXR_LIB_PREFIX - which defaulted to "lib",
# even on windows - WAS used for the .dll names.
#
# So, if PXR_LIB_PREFIX was left at it's default value of "lib", you
# had output libs like:
#    usd.lib
#    libusd.dll
#
# The upshot is that, for windows and USD <= 0.19.11, you probably want to
# leave USD_LIB_PREFIX at it's default (empty string on windows), even if you
# set a PXR_LIB_PREFIX when building USD core.

#set(USD_LIB_PREFIX "libpxr_"
set(USD_LIB_PREFIX ${CMAKE_SHARED_LIBRARY_PREFIX}
    CACHE STRING "Prefix of USD libraries")

find_path(USD_INCLUDE_DIR pxr/pxr.h
    HINTS
    $ENV{USD_INCLUDE_DIR}
    ${USD_ROOT}/include
    $ENV{USD_ROOT}/include
    DOC "USD Include directory")

    

# Disabled because this FindUSD doesn't work with pxrConfig.cmake - see note
# below

# find_file(USD_CONFIG_FILE
#           names pxrConfig.cmake
#           PATHS ${USD_ROOT}
#                 $ENV{USD_ROOT}
#           DOC "USD cmake configuration file")

# We need to find either usd or usd_ms (the monolithic-shared library),
# with taking the prefix into account.
find_path(USD_LIBRARY_DIR
    NAMES
        ${USD_LIB_PREFIX}usd${USD_LIB_SUFFIX}
        ${USD_LIB_PREFIX}usd_ms${USD_LIB_SUFFIX}
    HINTS
        $ENV{USD_LIBRARY_DIR}
        ${USD_ROOT}/lib
        $ENV{USD_ROOT}/lib
    DOC "USD Libraries directory")

find_file(USD_GENSCHEMA
    NAMES usdGenSchema
    HINTS
        ${USD_ROOT}
        $ENV{USD_ROOT}
    PATH_SUFFIXES
        bin
    DOC "USD Gen schema application")

# USD Maya components

find_path(USD_MAYA_INCLUDE_DIR usdMaya/api.h
    HINTS
        # If we're using Autodesk Maya-USD repo
        ${MAYA_USD_ROOT}/plugin/pxr/maya/include
        $ENV{MAYA_USD_ROOT}/plugin/pxr/maya/include

        # If we're using Pixar USD core repo (<=0.19.11)
        ${USD_ROOT}/third_party/maya/include
        $ENV{USD_ROOT}/third_party/maya/include
        ${USD_MAYA_ROOT}/third_party/maya/include
        $ENV{USD_MAYA_ROOT}/third_party/maya/include
    DOC "USD Maya Include directory")

find_path(USD_MAYA_LIBRARY_DIR
    NAMES
        # If we're using Autodesk Maya-USD repo
        ${CMAKE_SHARED_LIBRARY_PREFIX}usdMaya${USD_LIB_SUFFIX}

        # If we're using Pixar USD core repo (<=0.19.11)
        ${USD_LIB_PREFIX}usdMaya${USD_LIB_SUFFIX}
    HINTS
        # If we're using Autodesk Maya-USD repo
        ${MAYA_USD_ROOT}/plugin/pxr/maya/lib
        $ENV{MAYA_USD_ROOT}/plugin/pxr/maya/lib

        # If we're using Pixar USD core repo (<=0.19.11)
        ${USD_ROOT}/third_party/maya/lib
        $ENV{USD_ROOT}/third_party/maya/lib
        ${USD_MAYA_ROOT}/third_party/maya/lib
        $ENV{USD_MAYA_ROOT}/third_party/maya/lib
    DOC "USD Maya Library directory")

# Maya USD (autodesk repo) - only components

find_path(MAYA_USD_INCLUDE_DIR mayaUsd/mayaUsd.h
    HINTS
        # If we're using Autodesk Maya-USD repo
        ${MAYA_USD_ROOT}/include
        $ENV{MAYA_USD_ROOT}/include
    DOC "Maya-USD Core Include directory")

find_path(MAYA_USD_LIBRARY_DIR
    NAMES
    	mayaUsd
    HINTS
        # If we're using Autodesk Maya-USD repo
        ${MAYA_USD_ROOT}/lib
        $ENV{MAYA_USD_ROOT}/lib
    DOC "USD Maya Library directory")

# USD Katana components

find_path(USD_KATANA_INCLUDE_DIR usdKatana/api.h
    HINTS
        ${USD_ROOT}/third_party/katana/include
        $ENV{USD_ROOT}/third_party/katana/include
        ${USD_KATANA_ROOT}/third_party/katana/include
        $ENV{USD_KATANA_ROOT}/third_party/katana/include
    DOC "USD Katana Include directory")

find_path(USD_KATANA_LIBRARY_DIR
    NAMES
        ${USD_LIB_PREFIX}usdKatana${USD_LIB_SUFFIX}
    HINTS
        ${USD_ROOT}/third_party/katana/lib
        $ENV{USD_ROOT}/third_party/katana/lib
        ${USD_KATANA_ROOT}/third_party/katana/lib
        $ENV{USD_KATANA_ROOT}/third_party/katana/lib
    DOC "USD Katana Library directory")

# USD Houdini components

find_path(USD_HOUDINI_INCLUDE_DIR gusd/api.h
    HINTS
        ${USD_ROOT}/third_party/houdini/include
        $ENV{USD_ROOT}/third_party/houdini/include
        ${USD_HOUDINI_ROOT}/third_party/houdini/include
        $ENV{USD_HOUDINI_ROOT}/third_party/houdini/include
    DOC "USD Houdini Include directory")

find_path(USD_HOUDINI_LIBRARY_DIR
    NAMES
        ${USD_LIB_PREFIX}gusd${USD_LIB_SUFFIX}
    HINTS
        ${USD_ROOT}/third_party/houdini/lib
        $ENV{USD_ROOT}/third_party/houdini/lib
        ${USD_HOUDINI_ROOT}/third_party/houdini/lib
        $ENV{USD_HOUDINI_ROOT}/third_party/houdini/lib
    DOC "USD Houdini Library directory")

if(USD_INCLUDE_DIR AND EXISTS "${USD_INCLUDE_DIR}/pxr/pxr.h")
    foreach(_usd_comp MAJOR MINOR PATCH)
        file(STRINGS
            "${USD_INCLUDE_DIR}/pxr/pxr.h"
            _usd_tmp
            REGEX "#define PXR_${_usd_comp}_VERSION .*$")
        string(REGEX MATCHALL "[0-9]+" USD_${_usd_comp}_VERSION ${_usd_tmp})
    endforeach()
    set(USD_VERSION ${USD_MAJOR_VERSION}.${USD_MINOR_VERSION}.${USD_PATCH_VERSION})
    set(USD_MAJOR_VERSION ${USD_MAJOR_VERSION})
    set(USD_MINOR_VERSION ${USD_MINOR_VERSION})
    set(USD_PATCH_VERSION ${USD_PATCH_VERSION})
endif()

# NOTE: setting the usd libs to be INTERFACE IMPORTED targets conflicts with
#       usage of pxrConfig.cmake - so if you are using this FindUSD, you
#       currently can't use pxrConfig.cmake.  You could comment out / remove
#       these sections to allow usage of pxrConfig.cmake.
#       We considered using pxrConfig.cmake, but we don't like the fact that it
#       bakes in full paths to the various dependencies

set(USD_LIBS ar;arch;cameraUtil;garch;gf;glf;hd;hdSt;hdx;hf;hgi;hgiGL;hio;js;kind;ndr;pcp;plug;pxOsd;sdf;sdr;tf;trace;usd;usdAppUtils;usdGeom;usdHydra;usdImaging;usdImagingGL;usdLux;usdRender;usdRi;usdShade;usdShaders;usdSkel;usdSkelImaging;usdUI;usdUtils;usdviewq;usdVol;usdVolImaging;vt;work;usd_ms)

foreach (lib ${USD_LIBS})
    find_library(USD_${lib}_LIBRARY
        NAMES ${USD_LIB_PREFIX}${lib}${USD_LIB_SUFFIX}
        HINTS ${USD_LIBRARY_DIR})
    if (USD_${lib}_LIBRARY)
        add_library(${lib} INTERFACE IMPORTED)
        set_target_properties(${lib}
            PROPERTIES
            INTERFACE_LINK_LIBRARIES ${USD_${lib}_LIBRARY}
        )
        list(APPEND USD_LIBRARIES ${USD_${lib}_LIBRARY})
    endif ()
endforeach ()

set(USD_MAYA_LIBS px_vp20;pxrUsdMayaGL;usdMaya)

foreach (lib ${USD_MAYA_LIBS})
    find_library(USD_MAYA_${lib}_LIBRARY
        NAMES
            # If we're using Autodesk Maya-USD repo
            ${lib}${USD_LIB_SUFFIX}

            # If we're using Pixar USD core repo (<=0.19.11)
            ${USD_LIB_PREFIX}${lib}${USD_LIB_SUFFIX}
        HINTS ${USD_MAYA_LIBRARY_DIR})
    if (USD_MAYA_${lib}_LIBRARY)
        add_library(${lib} INTERFACE IMPORTED)
        set_target_properties(${lib}
            PROPERTIES
            INTERFACE_LINK_LIBRARIES ${USD_MAYA_${lib}_LIBRARY}
        )
        list(APPEND USD_MAYA_LIBRARIES ${USD_MAYA_${lib}_LIBRARY})
    endif ()
endforeach ()

set(USD_KATANA_LIBS usdKatana;vtKatana)

foreach (lib ${USD_KATANA_LIBS})
    find_library(USD_KATANA_${lib}_LIBRARY
        NAMES ${USD_LIB_PREFIX}${lib}${USD_LIB_SUFFIX}
        HINTS ${USD_KATANA_LIBRARY_DIR})
    if (USD_KATANA_${lib}_LIBRARY)
        add_library(${lib} INTERFACE IMPORTED)
        set_target_properties(${lib}
            PROPERTIES
            INTERFACE_LINK_LIBRARIES ${USD_KATANA_${lib}_LIBRARY}
        )
        list(APPEND USD_KATANA_LIBRARIES ${USD_KATANA_${lib}_LIBRARY})
    endif ()
endforeach ()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(USD
    REQUIRED_VARS
        USD_INCLUDE_DIR
        USD_LIBRARY_DIR
        USD_LIBRARIES
        #USD_MAJOR_VERSION
        USD_MINOR_VERSION
        USD_PATCH_VERSION
    VERSION_VAR
        USD_VERSION)