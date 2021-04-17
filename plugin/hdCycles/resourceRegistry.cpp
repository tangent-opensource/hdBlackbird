//  Copyright 2021 Tangent Animation
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

#include "resourceRegistry.h"

#include <pxr/base/work/loops.h>
#include <pxr/usd/sdf/path.h>

#include <render/scene.h>

PXR_NAMESPACE_USING_DIRECTIVE

void
HdCyclesResourceRegistry::_Commit()
{
    ccl::thread_scoped_lock scene_lock { m_scene->mutex };

    // 1) bind objects to the scene
    for (auto& object_source : m_object_sources) {
        if (!object_source.second.value->IsValid()) {
            continue;
        }
        object_source.second.value->Resolve();
    }

    // 2) commit all pending resources
    using ValueType = HdInstanceRegistry<HdCyclesObjectSourceSharedPtr>::const_iterator::value_type;
    WorkParallelForEach(m_object_sources.begin(), m_object_sources.end(), [](const ValueType& object_source) {
        // resolve per object
        object_source.second.value->ResolvePendingSources();
    });
}

void
HdCyclesResourceRegistry::_GarbageCollect()
{
    ccl::thread_scoped_lock scene_lock { m_scene->mutex };

    m_object_sources.GarbageCollect();
}

HdInstance<HdCyclesObjectSourceSharedPtr>
HdCyclesResourceRegistry::GetObjectInstance(const SdfPath& id)
{
    return m_object_sources.GetInstance(id.GetHash());
}
