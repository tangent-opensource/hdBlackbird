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

#ifdef Houdini_FOUND
#   include <GT/GT_PrimVDB.h>
#endif

#include "openvdb_asset.h"

#include <pxr/base/arch/library.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

HdCyclesVolumeLoader::HdCyclesVolumeLoader(const char* filepath, const char* grid_name_in)
    : ccl::VDBImageLoader(grid_name_in)
    , m_file_path(filepath)
{
#ifdef Houdini_FOUND
    auto hfs = std::getenv("HFS");
    if(hfs){
        auto lib_path = hfs + std::string("/houdini/dso/USD_SopVol") + ARCH_LIBRARY_SUFFIX;
        m_sopVolLibHandle = ArchLibraryOpen(lib_path, ARCH_LIBRARY_LAZY);

        if (m_sopVolLibHandle) {
            m_houdiniVdbLoader = (houdiniVdbLoadFunc)ArchLibraryGetSymbolAddress(m_sopVolLibHandle, "SOPgetVDBVolumePrimitive");
            if (!m_houdiniVdbLoader) {
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
#endif

    UpdateGrid();
}

HdCyclesVolumeLoader::~HdCyclesVolumeLoader()
{
    if (m_sopVolLibHandle) {
        ArchLibraryClose(m_sopVolLibHandle);
    }
}

void
HdCyclesVolumeLoader::UpdateGrid()
{
    if (TF_VERIFY(!m_file_path.empty())) {
        try {
#ifdef Houdini_FOUND
            if (grid) {
                grid.reset();
            }

            // Load vdb grid from memory if the filepath is pointing to a houdini sop
            static std::string opPrefix("op:");
            if (m_file_path.compare(0, opPrefix.size(), opPrefix) == 0) {
                auto vdbPrim = reinterpret_cast<GT_PrimVDB*>(
                    (*m_houdiniVdbLoader)(m_file_path.c_str(), grid_name.c_str()));
                this->grid = vdbPrim->getGrid()->copyGrid();
            } else {
                openvdb::io::File file(m_file_path);
                file.setCopyMaxBytes(0);
                file.open();

                this->grid = file.readGrid(grid_name);
            }

            if (!grid) {
                TF_WARN("Vdb grid is empty!");
            }
#else
            openvdb::io::File file(m_file_path);
            file.setCopyMaxBytes(0);
            file.open();

            if (grid) {
                grid.reset();
            }

            this->grid = file.readGrid(grid_name);
#endif
        } catch (const openvdb::IoError& e) {
            TF_RUNTIME_ERROR("Unable to load grid %s from file %s", grid_name.c_str(), m_file_path.c_str());
        } catch (const std::exception& e) {
            TF_RUNTIME_ERROR("Error updating grid: %s", e.what());
        }
    } else {
        TF_WARN("Volume file path is empty!");
    }
}

HdCyclesOpenvdbAsset::HdCyclesOpenvdbAsset(HdCyclesRenderDelegate* a_delegate, const SdfPath& id)
    : HdField(id)
{
    TF_UNUSED(a_delegate);
}

void
HdCyclesOpenvdbAsset::Sync(HdSceneDelegate* a_sceneDelegate, HdRenderParam* a_renderParam, HdDirtyBits* a_dirtyBits)
{
    TF_UNUSED(a_renderParam);
    if (*a_dirtyBits & HdField::DirtyParams) {
        auto& changeTracker = a_sceneDelegate->GetRenderIndex().GetChangeTracker();
        // But accessing this list happens on a single thread,
        // as bprims are synced before rprims.
        for (const auto& volume : _volumeList) {
            changeTracker.MarkRprimDirty(volume, HdChangeTracker::DirtyTopology);
        }
    }
    *a_dirtyBits = HdField::Clean;
}

HdDirtyBits
HdCyclesOpenvdbAsset::GetInitialDirtyBitsMask() const
{
    return HdField::AllDirty;
}

// This will be called from multiple threads.
void
HdCyclesOpenvdbAsset::TrackVolumePrimitive(const SdfPath& id)
{
    std::lock_guard<std::mutex> lock(_volumeListMutex);
    _volumeList.insert(id);
}

PXR_NAMESPACE_CLOSE_SCOPE