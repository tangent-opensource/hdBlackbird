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

#include "renderPassState.h"
#include "camera.h"
#include "resourceRegistry.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdCyclesRenderPassState::HdCyclesRenderPassState(const HdCyclesRenderDelegate* renderDelegate)
    : m_renderDelegate { renderDelegate }
{
}

void
HdCyclesRenderPassState::Prepare(const HdResourceRegistrySharedPtr& resourceRegistry)
{
    HdCyclesResourceRegistry* registry = dynamic_cast<HdCyclesResourceRegistry*>(resourceRegistry.get());
    if(!registry) {
        return;
    }

    // TODO: Resource registry is a shared pointer that is const, but the pointer held is not const!
    //       We can continue committing camera changes from here. That means no need to cast away const correctness
    //       for the camera. The camera render parameters are set through HdxRenderSetupTask::PrepareCamera.
    //       All code related to camera update should be moved here, and committed to resource registry as pending
    //       source.
}
