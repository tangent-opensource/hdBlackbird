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

add_executable(blackbirdengine
        engine.cpp
        engine.h
        )

target_include_directories(blackbirdengine
        PUBLIC
        ..
        )

target_link_libraries(blackbirdengine
        PUBLIC
        Cycles::Cycles
        Usd::Usd
        hdCycles
        usdCycles
        )

target_compile_options(blackbirdengine
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4273 /Zi /experimental:external /external:W0>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/Ob0 /Od>
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -pedantic -Wno-deprecated -Wshadow -Wdouble-promotion -Wconversion -Wnull-dereference -Wold-style-cast -Wuseless-cast>
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<CONFIG:Debug>>:-g3>
        )

target_link_options(blackbirdengine
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/ignore:4217 /ignore:4049>
        )