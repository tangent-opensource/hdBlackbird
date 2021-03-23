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

if(USE_LEGACY_HOUDINI)
  include_directories(SYSTEM ${HOUDINI_INCLUDE_DIRS})
  link_directories(${HOUDINI_LIB_DIRS})
  link_libraries(${HOUDINI_LIBRARIES_DEPS})
endif()

if(USE_OPENGL)
  include_directories(SYSTEM ${GLEW_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})
  link_libraries(${OPENGL_gl_LIBRARY} ${GLEW_LIBRARY})
endif()

add_library(ndrCycles SHARED
        discovery.cpp
        discovery.h
        parser.cpp
        parser.h
        api.h
        )

target_include_directories(ndrCycles SYSTEM
        PUBLIC
        ${USD_INCLUDE_DIR}
        ${Boost_INCLUDE_DIRS}
        ${HBoost_INCLUDE_DIRS}
        ${Python_INCLUDE_DIRS}
        ${PYTHON_INCLUDE_DIR}
        ${CYCLES_INCLUDE_DIRS}
        ${OIIO_INCLUDE_DIRS}
        ${OPENEXR_INCLUDE_DIRS}
        ${TBB_INCLUDE_DIRS}
        )

target_link_libraries(ndrCycles
        PUBLIC
        ${USD_LIBRARIES}
        ${TBB_LIBRARIES}
        ${Boost_LIBRARIES}
        ${HBoost_LIBRARIES}
        ${Python_LIBRARIES}
        ${CYCLES_LIBRARIES}
        ${PNG_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${TIFF_LIBRARY}
        ${PTHREADS_LIBRARIES}
        ${OPENJPEG_LIBRARIES}
        ${OIIO_LIBRARIES}
        ${OPENEXR_LIBRARIES}
        )

target_compile_definitions(ndrCycles
        PRIVATE
        MFB_PACKAGE_NAME=ndrCycles
        MFB_ALT_PACKAGE_NAME=ndrCycles
        )

set_target_properties(ndrCycles PROPERTIES
        PREFIX ""
        POSITION_INDEPENDENT_CODE ON
        )
install(TARGETS ndrCycles DESTINATION plugin/usd/ndrCycles)
install(FILES $<TARGET_PDB_FILE:ndrCycles> DESTINATION plugin/usd/ndrCycles OPTIONAL)

# plugInfo.json
set(PLUG_INFO_LIBRARY_PATH "ndrCycles${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(PLUG_INFO_RESOURCE_PATH "resources")
set(PLUG_INFO_ROOT "..")
configure_file(plugInfo.json plugInfo.json @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" DESTINATION plugin/usd/ndrCycles/resources)
