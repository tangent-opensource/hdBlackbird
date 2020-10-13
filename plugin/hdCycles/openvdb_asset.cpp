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

#include "openvdb_asset.h"

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

HdCyclesOpenvdbAsset::HdCyclesOpenvdbAsset(HdCyclesRenderDelegate* a_delegate,
                                           const SdfPath& id)
    : HdField(id)
{
    TF_UNUSED(a_delegate);
}

void
HdCyclesOpenvdbAsset::Sync(HdSceneDelegate* a_sceneDelegate,
                           HdRenderParam* a_renderParam,
                           HdDirtyBits* a_dirtyBits)
{
    TF_UNUSED(a_renderParam);
    if (*a_dirtyBits & HdField::DirtyParams) {
        auto& changeTracker
            = a_sceneDelegate->GetRenderIndex().GetChangeTracker();
        // But accessing this list happens on a single thread,
        // as bprims are synced before rprims.
        for (const auto& volume : _volumeList) {
            changeTracker.MarkRprimDirty(volume,
                                         HdChangeTracker::DirtyTopology);
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