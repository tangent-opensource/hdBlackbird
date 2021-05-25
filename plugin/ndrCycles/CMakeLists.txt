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

add_library(ndrCycles SHARED
        discovery.cpp
        discovery.h
        parser.cpp
        parser.h
        api.h
        )

target_include_directories(ndrCycles
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../
        )

target_link_libraries(ndrCycles
        PUBLIC
        Cycles::Cycles
        Usd::Usd
        )

target_compile_options(ndrCycles
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4273 /Zi /experimental:external /external:W0>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/Ob0 /Od>
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -pedantic -Wno-deprecated -Wshadow -Wdouble-promotion -Wconversion -Wnull-dereference -Wold-style-cast -Wuseless-cast>
        )

target_compile_definitions(ndrCycles
        PRIVATE
        MFB_PACKAGE_NAME=ndrCycles
        MFB_ALT_PACKAGE_NAME=ndrCycles
        )

set_target_properties(ndrCycles PROPERTIES
        PREFIX ""
        )
install(TARGETS ndrCycles DESTINATION plugin/usd)
if (MSVC)
    install(FILES $<TARGET_PDB_FILE:ndrCycles> DESTINATION plugin/usd OPTIONAL)
endif ()

# plugInfo.json
set(PLUG_INFO_LIBRARY_PATH "../ndrCycles${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(PLUG_INFO_RESOURCE_PATH "resources")
set(PLUG_INFO_ROOT "..")
configure_file(plugInfo.json plugInfo.json @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" DESTINATION plugin/usd/ndrCycles/resources)
