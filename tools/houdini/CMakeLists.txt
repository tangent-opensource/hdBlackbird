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
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

set(TOOL_NAME hda_generator)
project(${TOOL_NAME})

set(CMAKE_BINARY_DIR $ENV{REZ_BUILD_PATH}/tools)
set(EXECUTABLE_OUTPUT_PATH $ENV{REZ_BUILD_PATH}/tools)

link_directories(${HOUDINI_LIB_DIRS})
link_libraries(${HOUDINI_LIBRARIES_DEPS})

add_executable(${TOOL_NAME} hda_generator.cpp)

target_include_directories(${TOOL_NAME} PRIVATE 
  ${CYCLES_INCLUDE_DIRS}
  ${HOUDINI_INCLUDE_DIRS}
  ${OPENEXR_INCLUDE_DIRS}
  ${USD_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${Python_INCLUDE_DIRS}
  ${CYCLES_INCLUDE_DIRS}
  ${OPENEXR_INCLUDE_DIRS}
  ${GLEW_INCLUDE_DIR}
)

target_link_libraries(${TOOL_NAME}
  ${TBB_LIBRARIES} 
  ${Boost_LIBRARIES} 
  ${Python_LIBRARIES} 
  ${CYCLES_LIBRARIES} 
  ${OPENGL_gl_LIBRARY}
  ${GLEW_LIBRARY} 
  ${OPENJPEG_LIBRARIES} 
  ${OPENEXR_LIBRARIES} 
  ${OPENSUBDIV_LIBRARIES}
  ${OIIO_LIBRARIES} 
)

install(TARGETS ${TOOL_NAME} RUNTIME DESTINATION $ENV{REZ_BUILD_INSTALL_PATH}/build/tools)
