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

#ifndef HDCYCLES_RESOURCEREGISTRY_H
#define HDCYCLES_RESOURCEREGISTRY_H

#include "api.h"

#include <pxr/imaging/hd/resourceRegistry.h>

namespace ccl {
class Scene;
}

PXR_NAMESPACE_OPEN_SCOPE

///
/// Cycles Resource Registry commits resources to Cycles
///
class HdCyclesResourceRegistry final : public HdResourceRegistry {
public:
    explicit HdCyclesResourceRegistry();

    void UpdateScene(ccl::Scene* scene) { m_scene = scene; }

private:
    void _Commit() override;
    void _GarbageCollect() override;

    ccl::Scene* m_scene;
};

using HdCyclesResourceRegistrySharedPtr = std::shared_ptr<HdCyclesResourceRegistry>;

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_RESOURCEREGISTRY_H