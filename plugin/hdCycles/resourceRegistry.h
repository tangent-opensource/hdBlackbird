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

#ifndef HDCYCLES_RESOURCEREGISTRY_H
#define HDCYCLES_RESOURCEREGISTRY_H

#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/instanceRegistry.h>
#include <pxr/imaging/hd/bufferSource.h>

#include <tbb/concurrent_vector.h>

namespace ccl {
class Scene;
}

PXR_NAMESPACE_OPEN_SCOPE


class HdCyclesResourceRegistry final : public HdResourceRegistry {
public:

    HdCyclesResourceRegistry() = default;

    void UpdateScene(ccl::Scene* scene) { m_scene = scene; }

    void AddSource(HdBufferSourceSharedPtr source);

private:
    void _Commit() override;
    void _GarbageCollect() override;

    ccl::Scene* m_scene{};

    tbb::concurrent_vector<HdBufferSourceSharedPtr> m_pending_sources;
};

using HdCyclesResourceRegistrySharedPtr = std::shared_ptr<HdCyclesResourceRegistry>;

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_RESOURCEREGISTRY_H
