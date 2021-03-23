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

if(USE_USD_CYCLES_SCHEMA)
    include_directories(SYSTEM ${USD_CYCLES_INCLUDE_DIRS})
    link_libraries(${USD_CYCLES_LIBRARIES})
endif()

if(USE_LEGACY_HOUDINI)
    include_directories(SYSTEM  ${HOUDINI_INCLUDE_DIRS})
    link_directories(${HOUDINI_LIB_DIRS})
    link_libraries(${HOUDINI_LIBRARIES_DEPS})
endif()

if(WITH_OPENVDB)
    include_directories(SYSTEM ${OPENVDB_INCLUDE_DIR})
    link_libraries(${OPENVDB_LIBRARY})
endif()

if(WITH_EMBREE)
    include_directories(SYSTEM ${EMBREE_INCLUDE_DIRS})
    link_libraries(${EMBREE_LIBRARIES})
endif()

if(USE_OPENGL)
    include_directories(SYSTEM ${GLEW_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})
    link_libraries(${OPENGL_gl_LIBRARY} ${GLEW_LIBRARY})
endif()

add_library(MikktSpace STATIC
        Mikktspace/mikktspace.c
        Mikktspace/mikktspace.h
        )
add_library(MikktSpace::MikktSpace ALIAS MikktSpace)

set_target_properties(MikktSpace PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        )

add_library(hdCycles SHARED
        api.h
        debug_codes.h
        basisCurves.cpp
        basisCurves.h
        camera.cpp
        camera.h
        config.cpp
        config.h
        hdcycles.h
        instancer.cpp
        instancer.h
        light.cpp
        light.h
        material.cpp
        material.h
        mesh.cpp
        mesh.h
        meshRefiner.cpp
        meshRefiner.h
        openvdb_asset.cpp
        openvdb_asset.h
        points.cpp
        points.h
        renderBuffer.cpp
        renderBuffer.h
        renderDelegate.cpp
        renderDelegate.h
        rendererPlugin.cpp
        rendererPlugin.h
        renderParam.cpp
        renderParam.h
        renderPass.cpp
        renderPass.h
        utils.cpp
        utils.h
        volume.cpp
        volume.h
        )

target_include_directories(hdCycles SYSTEM
        PUBLIC
        ${USD_INCLUDE_DIR}
        ${Boost_INCLUDE_DIRS}
        ${HBoost_INCLUDE_DIRS}
        ${TBB_INCLUDE_DIRS}
        ${Python_INCLUDE_DIRS}
        ${PYTHON_INCLUDE_DIR}
        ${CYCLES_INCLUDE_DIRS}
        ${OIIO_INCLUDE_DIRS}
        ${OPENEXR_INCLUDE_DIRS}
        ${OPENSUBDIV_INCLUDE_DIR}
        ${OCIO_INCLUDE_DIRS}
        ${GFLAGS_INCLUDE_DIR}
        ${GLOG_INCLUDE_DIR}
        )

target_link_libraries(hdCycles
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
        ${OPENJPEG_LIBRARIES}
        ${OPENEXR_LIBRARIES}
        ${OPENSUBDIV_LIBRARIES}
        ${OIIO_LIBRARIES}
        ${OCIO_LIBRARIES}

        ${GLOG_LIBRARY}
        ${GFLAGS_LIBRARY}
        MikktSpace::MikktSpace
        )

target_compile_definitions(hdCycles
        PRIVATE
        MFB_PACKAGE_NAME=hdCycles
        MFB_ALT_PACKAGE_NAME=hdCycles
        )

set_target_properties(hdCycles PROPERTIES
        PREFIX ""
        POSITION_INDEPENDENT_CODE ON
        )
install(TARGETS hdCycles DESTINATION plugin/usd/hdCycles)
install(FILES $<TARGET_PDB_FILE:hdCycles> DESTINATION plugin/usd/hdCycles OPTIONAL)


# plugInfo.json
set(PLUG_INFO_LIBRARY_PATH "hdCycles${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(PLUG_INFO_RESOURCE_PATH "resources")
set(PLUG_INFO_ROOT "..")
configure_file(plugInfo.json plugInfo.json @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" DESTINATION plugin/usd/hdCycles/resources)
