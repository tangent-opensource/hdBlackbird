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

#include "sceneBinding.h"

#include <render/scene.h>

PXR_NAMESPACE_USING_DIRECTIVE

HdCyclesShaderBinding::HdCyclesShaderBinding(ccl::Scene* scene, ccl::Shader* shader)
    : m_bound { false }
    , m_scene { scene }
    , m_shader { shader }
{
}

HdCyclesShaderBinding::~HdCyclesShaderBinding()
{
    for (auto it = m_scene->shaders.begin(); it != m_scene->shaders.end();) {
        if (m_shader == *it) {
            it = m_scene->shaders.erase(it);
            break;
        } else {
            ++it;
        }
    }
}

bool
HdCyclesShaderBinding::Bind()
{
    if (m_bound) {
        return true;
    }
    m_scene->shaders.push_back(m_shader);
    m_bound = true;
}
