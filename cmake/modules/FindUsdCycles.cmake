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

set(USD_CYCLES_LIB_PREFIX "")
set(USD_CYCLES_LIB_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX})

if(UNIX)
    # Not used currently because linux doesnt build with lib prefix
    #set(USD_CYCLES_LIB_PREFIX lib)

    set(USD_CYCLES_LIB_SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX})
endif()

# Cycles Includes

find_path(USD_CYCLES_INCLUDE_DIRS "usdCycles/tokens.h"
    HINTS
        ${USD_CYCLES_ROOT}/include
        $ENV{USD_CYCLES_ROOT}/include
    DOC "usdCycles Include directory")

# Cycles Libraries

find_path(USD_CYCLES_LIBRARY_DIR
    NAMES
    ${USD_CYCLES_LIB_PREFIX}usdCycles${USD_CYCLES_LIB_SUFFIX}

    HINTS
    ${USD_CYCLES_ROOT}/plugin/usd
    $ENV{USD_CYCLES_ROOT}/plugin/usd

    DOC "usdCycles Libraries directory")

set(USD_CYCLES_LIBS usdCycles)

foreach (lib ${USD_CYCLES_LIBS})
    find_library(${lib}_LIBRARY
        NAMES ${USD_CYCLES_LIB_PREFIX}${lib}${USD_CYCLES_LIB_SUFFIX}
        HINTS ${USD_CYCLES_LIBRARY_DIR})
    if (${lib}_LIBRARY)
        add_library(${lib} INTERFACE IMPORTED)
        set_target_properties(${lib}
            PROPERTIES
            INTERFACE_LINK_LIBRARIES ${${lib}_LIBRARY}
        )
        list(APPEND USD_CYCLES_LIBRARIES ${${lib}_LIBRARY})
    endif ()
endforeach ()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(UsdCycles
    REQUIRED_VARS
        USD_CYCLES_INCLUDE_DIRS
        USD_CYCLES_LIBRARY_DIR
        USD_CYCLES_LIBRARIES
    )
