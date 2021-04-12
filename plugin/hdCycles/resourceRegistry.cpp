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

#include <render/scene.h>

PXR_NAMESPACE_USING_DIRECTIVE

void
HdCyclesResourceRegistry::_Commit()
{
    ccl::thread_scoped_lock scene_lock { m_scene->mutex };

    // resolve pending sources
    for (auto& source : m_pending_sources) {
        if (!source->IsValid()) {
            continue;
        }
        source->Resolve();
    }
}

void
HdCyclesResourceRegistry::_GarbageCollect()
{
    ccl::thread_scoped_lock scene_lock { m_scene->mutex };

    // cleanup pending sources
    m_pending_sources.clear();
}

void
HdCyclesResourceRegistry::AddSource(HdBufferSourceSharedPtr source)
{
    m_pending_sources.push_back(std::move(source));
}
