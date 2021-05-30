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

#ifndef HOUDINI_VDB_LOADER_H
#define HOUDINI_VDB_LOADER_H

#include "pxr/pxr.h"

#include <openvdb/openvdb.h>

PXR_NAMESPACE_OPEN_SCOPE

class HoudiniVdbLoader {
public:
    static HoudiniVdbLoader const& Instance() {
        static HoudiniVdbLoader instance;
        return instance;
    }

    ~HoudiniVdbLoader();

    openvdb::GridBase::ConstPtr const GetGrid(const char* filepath, const char* name) const;

private:
    HoudiniVdbLoader();

private:
    void* m_sopVolLibHandle = nullptr;

    typedef void* (*sopVdbGetterFunction)(const char* filepath, const char* name);
    sopVdbGetterFunction m_vdbGetter = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HOUDINI_VDB_LOADER_H
