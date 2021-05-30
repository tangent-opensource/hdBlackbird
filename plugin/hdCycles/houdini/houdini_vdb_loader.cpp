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

#include "houdini_vdb_loader.h"

#include "pxr/base/arch/library.h"
#include "pxr/base/tf/diagnostic.h"

#include <GT/GT_PrimVDB.h>

#ifdef WIN32
#include <Windows.h>
#define GETSYM(handle, name) GetProcAddress((HMODULE)handle, name)
#else
#include <dlfcn.h>
#define GETSYM(handle, name) dlsym(handle, name)
#endif

PXR_NAMESPACE_OPEN_SCOPE

HoudiniVdbLoader::~HoudiniVdbLoader() {
    if (m_sopVolLibHandle) {
        ArchLibraryClose(m_sopVolLibHandle);
    }
}

openvdb::GridBase::ConstPtr const HoudiniVdbLoader::GetGrid(const char* filepath, const char* name) const {
    if (!m_vdbGetter) {
        return nullptr;
    }
    auto vdbPrim = reinterpret_cast<GT_PrimVDB*>((*m_vdbGetter)(filepath, name));
    if(!vdbPrim){
        return nullptr;
    }

    const auto* grid_base = vdbPrim->getGrid();
    return grid_base ? grid_base->copyGrid() : nullptr;
}

HoudiniVdbLoader::HoudiniVdbLoader() {
    if (auto hfs = std::getenv("HFS")) {
        auto sopVdbLibPath = hfs + std::string("/houdini/dso/USD_SopVol") + ARCH_LIBRARY_SUFFIX;
        m_sopVolLibHandle = ArchLibraryOpen(sopVdbLibPath, ARCH_LIBRARY_LAZY);
        if (m_sopVolLibHandle) {
            m_vdbGetter = (sopVdbGetterFunction)GETSYM(m_sopVolLibHandle, "SOPgetVDBVolumePrimitive");
            if (!m_vdbGetter) {
                TF_RUNTIME_ERROR("USD_SopVol missing required symbol: SOPgetVDBVolumePrimitive");
            }
        } else {
            auto err = ArchLibraryError();
            if (err.empty()) {
                err = "unknown reason";
            }
            TF_RUNTIME_ERROR("Failed to load USD_SopVol library: %s", err.c_str());
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE