//  Copyright 2020 Tangent Animation
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied,
//  including without limitation, as related to merchantability and fitness
//  for a particular purpose.
//
//  In no event shall any copyright holder be liable for any damages of any kind
//  arising from the use of this software, whether in contract, tort or otherwise.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef NDR_CYCLES_API_H
#define NDR_CYCLES_API_H

#include <embree3/rtcore.h>

#include <pxr/base/arch/export.h>

#if defined(PXR_STATIC)
#    define NDRCYCLES_API
#    define NDRCYCLES_API_TEMPLATE_CLASS(...)
#    define NDRCYCLES_API_TEMPLATE_STRUCT(...)
#    define NDRCYCLES_LOCAL
#else
#    if defined(NDRCYCLES_EXPORTS)
#        define NDRCYCLES_API ARCH_EXPORT
#        define NDRCYCLES_API_TEMPLATE_CLASS(...)                              \
            ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#        define NDRCYCLES_API_TEMPLATE_STRUCT(...)                             \
            ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#    else
#        define NDRCYCLES_API ARCH_IMPORT
#        define NDRCYCLES_API_TEMPLATE_CLASS(...)                              \
            ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#        define NDRCYCLES_API_TEMPLATE_STRUCT(...)                             \
            ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#    endif
#    define NDRCYCLES_LOCAL ARCH_HIDDEN
#endif

#endif  // NDR_CYCLES_API_H
