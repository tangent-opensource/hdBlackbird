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

if(DEFINED ENV{HDCYCLES_BUILD_VERSION_MAJOR})
    set(HD_CYCLES_MAJOR_VERSION "$ENV{HDCYCLES_BUILD_VERSION_MAJOR}")
else()
    set(HD_CYCLES_MAJOR_VERSION "0")
endif()

if(DEFINED ENV{HDCYCLES_BUILD_VERSION_MINOR})
    set(HD_CYCLES_MINOR_VERSION "$ENV{HDCYCLES_BUILD_VERSION_MINOR}")
else()
    set(HD_CYCLES_MINOR_VERSION "8")
endif()

if(DEFINED ENV{HDCYCLES_BUILD_VERSION_PATCH})
    set(HD_CYCLES_PATCH_VERSION "$ENV{HDCYCLES_BUILD_VERSION_PATCH}")
else()
    set(HD_CYCLES_PATCH_VERSION "x")
endif()

if(DEFINED ENV{HDCYCLES_BUILD_VERSION_PATCH})
    set(HD_CYCLES_VERSION "$ENV{HDCYCLES_BUILD_VERSION}")
else()
    set(HD_CYCLES_VERSION "0.8.x")
endif()

add_definitions("-DHD_CYCLES_MAJOR_VERSION=\"${HD_CYCLES_MAJOR_VERSION}\"")
add_definitions("-DHD_CYCLES_MINOR_VERSION=\"${HD_CYCLES_MINOR_VERSION}\"")
add_definitions("-DHD_CYCLES_PATCH_VERSION=\"${HD_CYCLES_PATCH_VERSION}\"")
add_definitions("-DHD_CYCLES_VERSION=\"${HD_CYCLES_VERSION}\"")
