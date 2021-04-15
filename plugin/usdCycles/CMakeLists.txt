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

set(USD_CYCLES_SCHEMA_OUTPUT_FILES
        # api
        api.h

        # tokens
        tokens.h
        tokens.cpp
        wrapTokens.cpp

        # render
        rendererSettingsAPI.h
        rendererSettingsAPI.cpp
        wrapRendererSettingsAPI.cpp

        # denoise
        denoiseSettingsAPI.h
        denoiseSettingsAPI.cpp
        wrapDenoiseSettingsAPI.cpp

        # integrator
        integratorSettingsAPI.h
        integratorSettingsAPI.cpp
        wrapIntegratorSettingsAPI.cpp

        # film
        filmSettingsAPI.h
        filmSettingsAPI.cpp
        wrapFilmSettingsAPI.cpp

        # light
        lightSettingsAPI.h
        lightSettingsAPI.cpp
        wrapLightSettingsAPI.cpp

        # camera
        cameraSettingsAPI.h
        cameraSettingsAPI.cpp
        wrapCameraSettingsAPI.cpp

        # object
        objectSettingsAPI.h
        objectSettingsAPI.cpp
        wrapObjectSettingsAPI.cpp

        # mesh
        meshSettingsAPI.h
        meshSettingsAPI.cpp
        wrapMeshSettingsAPI.cpp

        # material
        materialSettingsAPI.h
        materialSettingsAPI.cpp
        wrapMaterialSettingsAPI.cpp

        # background
        backgroundAPI.h
        backgroundAPI.cpp
        wrapBackgroundAPI.cpp

        # curve
        curveSettingsAPI.h
        curveSettingsAPI.cpp
        wrapCurveSettingsAPI.cpp

        # points
        pointsSettingsAPI.h
        pointsSettingsAPI.cpp
        wrapPointsSettingsAPI.cpp

        # bake
        bakeSettingsAPI.h
        bakeSettingsAPI.cpp
        wrapBakeSettingsAPI.cpp
        )

add_custom_command(OUTPUT ${USD_CYCLES_SCHEMA_OUTPUT_FILES} plugInfo.json generatedSchema.usda
        COMMAND ${USD_SCHEMA_GENERATOR} ${CMAKE_CURRENT_SOURCE_DIR}/schema.usda
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.cmake
        DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/schema.usda
        ${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.cmake
        VERBATIM
        )

add_library(usdCycles SHARED ${USD_CYCLES_SCHEMA_OUTPUT_FILES})

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
endif()
#

# plugInfo.json
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
        ${CMAKE_CURRENT_BINARY_DIR}/generatedSchema.usda
        DESTINATION plugin/usd/usdCycles/resources
        )

install(FILES schema.usda DESTINATION plugin/usd/usdCycles/resources/usdCycles)
