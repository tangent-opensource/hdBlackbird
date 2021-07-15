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

#ifndef HD_CYCLES_INSTANCER_H
#define HD_CYCLES_INSTANCER_H

#include "hdcycles.h"

#include <mutex>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/timeSampleArray.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;

/**
 * @brief Properly computes instance transforms for time varying data
 * Heavily inspired by ReadeonProRenderUSD's Instancer.cpp 
 * 
 */
class HdCyclesInstancer : public HdInstancer {
public:
    HdCyclesInstancer(HdSceneDelegate* delegate, SdfPath const& id, SdfPath const& parentInstancerId)
        : HdInstancer(delegate, id, parentInstancerId)
    {
    }

    VtMatrix4dArray ComputeTransforms(SdfPath const& prototypeId);

    HdTimeSampleArray<VtMatrix4dArray, HD_CYCLES_MOTION_STEPS> SampleInstanceTransforms(SdfPath const& prototypeId);

    void SyncPublic() { Sync(); }

private:
    void Sync();

    VtMatrix4dArray m_transform;
    VtVec3fArray m_translate;
    VtVec4fArray m_rotate;
    VtVec3fArray m_scale;

    std::mutex m_syncMutex;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_INSTANCER_H
