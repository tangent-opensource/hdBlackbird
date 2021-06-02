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

set(USD_CYCLES_SCHEMA_SOURCE_OUTPUT
        # api
        api.h

        # tokens
        tokens.h
        tokens.cpp

        # render
        rendererSettingsAPI.h
        rendererSettingsAPI.cpp

        # denoise
        denoiseSettingsAPI.h
        denoiseSettingsAPI.cpp

        # integrator
        integratorSettingsAPI.h
        integratorSettingsAPI.cpp

        # film
        filmSettingsAPI.h
        filmSettingsAPI.cpp

        # light
        lightSettingsAPI.h
        lightSettingsAPI.cpp

        # camera
        cameraSettingsAPI.h
        cameraSettingsAPI.cpp

        # object
        objectSettingsAPI.h
        objectSettingsAPI.cpp

        # mesh
        meshSettingsAPI.h
        meshSettingsAPI.cpp

        # material
        materialSettingsAPI.h
        materialSettingsAPI.cpp

        # background
        backgroundAPI.h
        backgroundAPI.cpp

        # curve
        curveSettingsAPI.h
        curveSettingsAPI.cpp

        # points
        pointsSettingsAPI.h
        pointsSettingsAPI.cpp

        # bake
        bakeSettingsAPI.h
        bakeSettingsAPI.cpp
        )


set(USD_CYCLES_SCHEMA_WRAP_OUTPUT
        wrapTokens.cpp
        wrapRendererSettingsAPI.cpp
        wrapDenoiseSettingsAPI.cpp
        wrapIntegratorSettingsAPI.cpp
        wrapFilmSettingsAPI.cpp
        wrapLightSettingsAPI.cpp
        wrapCameraSettingsAPI.cpp
        wrapObjectSettingsAPI.cpp
        wrapMeshSettingsAPI.cpp
        wrapMaterialSettingsAPI.cpp
        wrapBackgroundAPI.cpp
        wrapCurveSettingsAPI.cpp
        wrapPointsSettingsAPI.cpp
        wrapBakeSettingsAPI.cpp
        )

add_custom_command(OUTPUT
        ${USD_CYCLES_SCHEMA_SOURCE_OUTPUT}
        ${USD_CYCLES_SCHEMA_WRAP_OUTPUT}
        plugInfo.json generatedSchema.usda
        COMMAND ${USD_SCHEMA_GENERATOR} ${CMAKE_CURRENT_SOURCE_DIR}/schema.usda
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.cmake
        DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/schema.usda
        ${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.cmake
        VERBATIM
        )

# usdCycles

add_library(usdCycles SHARED ${USD_CYCLES_SCHEMA_SOURCE_OUTPUT})

target_include_directories(usdCycles
        PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/../
        )

target_link_libraries(usdCycles
        PUBLIC
        Cycles::Cycles
        Usd::Usd
        )

target_compile_options(usdCycles
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/wd4273 /Zi>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/Ob0 /Od>
        )

target_link_options(usdCycles
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/ignore:4217 /ignore:4049>
        )

target_compile_definitions(usdCycles
        PRIVATE
        MFB_PACKAGE_NAME=usdCycles
        MFB_ALT_PACKAGE_NAME=usdCycles
        )

set_target_properties(usdCycles PROPERTIES
        PREFIX ""
        )
install(TARGETS usdCycles DESTINATION plugin/usd)
if (MSVC)
    install(FILES $<TARGET_PDB_FILE:usdCycles> DESTINATION plugin/usd OPTIONAL)
endif ()

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
        ${CMAKE_CURRENT_BINARY_DIR}/generatedSchema.usda
        DESTINATION plugin/usd/usdCycles/resources
        )

install(FILES schema.usda DESTINATION plugin/usd/usdCycles/resources/usdCycles)

# _usdCycles - python

add_library(_usdCycles SHARED
        ${USD_CYCLES_SCHEMA_WRAP_OUTPUT}
        module.cpp
        moduleDeps.cpp
        )

target_include_directories(_usdCycles
        PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/../
        )

target_link_libraries(_usdCycles
        PUBLIC
        usdCycles
        )

target_compile_options(_usdCycles
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/wd4273 /Zi>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/Ob0 /Od>
        )

target_link_options(_usdCycles
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/ignore:4217 /ignore:4049>
        )

target_compile_definitions(_usdCycles
        PRIVATE
        MFB_PACKAGE_NAME=usdCycles
        MFB_ALT_PACKAGE_NAME=usdCycles
        )

set_target_properties(_usdCycles PROPERTIES
        PREFIX ""
        INSTALL_RPATH "$ORIGIN/../../usd"
        )

if (WIN32)
    set_target_properties(_usdCycles PROPERTIES SUFFIX ".pyd")
endif ()

install(TARGETS _usdCycles DESTINATION plugin/python/UsdCycles)
install(FILES __init__.py DESTINATION plugin/python/UsdCycles)
if (MSVC)
    install(FILES $<TARGET_PDB_FILE:_usdCycles> DESTINATION plugin/python/UsdCycles OPTIONAL)
endif ()
