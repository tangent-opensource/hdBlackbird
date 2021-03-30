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

#ifndef HDCYCLES_SCENEBINDING_H
#define HDCYCLES_SCENEBINDING_H

#include "api.h"

#include <memory>

namespace ccl {
class Scene;
class Shader;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

///
/// Cycles abstract scene binding
///
class HdCyclesSceneBinding {
public:
    HdCyclesSceneBinding() = default;

    virtual bool Bind()             = 0;
    virtual ~HdCyclesSceneBinding() = default;
};

using HdCyclesSceneBindingSharedPtr = std::shared_ptr<HdCyclesSceneBinding>;

///
/// Cycles shader to scene binding
///
class HdCyclesShaderBinding : public HdCyclesSceneBinding {
public:
    explicit HdCyclesShaderBinding(ccl::Scene* scene, ccl::Shader* shader);

    bool Bind() override;
    ~HdCyclesShaderBinding() override;

private:
    bool m_bound;
    ccl::Scene* m_scene;
    ccl::Shader* m_shader;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_SCENEBINDING_H
