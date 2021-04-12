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

#ifndef HD_CYCLES_OPENVDB_ASSET_H
#define HD_CYCLES_OPENVDB_ASSET_H

#include "api.h"
#include <pxr/pxr.h>

#include <pxr/imaging/hd/field.h>
#include "renderDelegate.h"

#include <mutex>
#include <unordered_set>

#ifdef WITH_OPENVDB
#    include <render/image_vdb.h>
#    include <openvdb/openvdb.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

#ifdef WITH_OPENVDB
// Very temporary. Apparently Cycles has code to do this but it isnt in the head cycles standalone repo
class HdCyclesVolumeLoader : public ccl::VDBImageLoader {
public:
    HdCyclesVolumeLoader(const char* filepath, const char* grid_name)
        : ccl::VDBImageLoader(grid_name)
        , m_file_path(filepath)
    {
        UpdateGrid();
    }

    void UpdateGrid()
    {
        if(TF_VERIFY(!m_file_path.empty()))
        {
            try {
                openvdb::io::File file(m_file_path);
                file.setCopyMaxBytes(0);
                file.open();

                if(grid){
                    grid.reset();
                }

                this->grid = file.readGrid(grid_name);
            } catch (const openvdb::IoError& e) {
                TF_RUNTIME_ERROR("Unable to load grid %s from file %s", grid_name, m_file_path);
            } catch(const std::exception& e) {
                TF_RUNTIME_ERROR("Error updating grid: %s", e.what());
            }
        }else{
            TF_WARN("Volume file path is empty!");
        }
    }

    void cleanup() override 
    {
        #ifdef WITH_NANOVDB
        nanogrid.reset();
        #endif
    }

private:
    std::string m_file_path;
};
#endif


/// Utility class for translating Hydra Openvdb Asset to Cycles Volume.
class HdCyclesOpenvdbAsset : public HdField {
public:
    /// Constructor for HdCyclesOpenvdbAsset
    ///
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the OpenVDB Asset.
        HdCyclesOpenvdbAsset(HdCyclesRenderDelegate* delegate, const SdfPath& id);

    /// Syncing the Hydra Openvdb Asset to the Cycles Volume.
    ///
    /// The functions main purpose is to dirty every Volume primitive's
    /// topology, so the grid definitions on the volume can be rebuilt, since
    /// changing the the grid name on the openvdb asset doesn't dirty the
    /// volume primitive, which holds the cycles volume shape.
    ///
    /// @param sceneDelegate Pointer to the Hydra Scene Delegate.
    /// @param renderParam Pointer to a HdCyclesRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
        void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
        HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Tracks a HdCyclesVolume primitive.
    ///
    /// Hydra separates the volume definitions from the grids each volume
    /// requires, so we need to make sure each grid definition, which can be
    /// shared between multiple volumes, knows which volume it belongs to.
    ///
    /// @param id Path to the Hydra Volume.
        void TrackVolumePrimitive(const SdfPath& id);

private:
    std::mutex _volumeListMutex;  ///< Lock for the _volumeList.
    /// Storing all the Hydra Volumes using this asset.
    std::unordered_set<SdfPath, SdfPath::Hash> _volumeList;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_OPENVDB_ASSET_H
