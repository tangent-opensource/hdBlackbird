/*
MIT License

Copyright (c) 2020 Piotr Barejko

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "resourceRegistry.h"

#include <pxr/base/work/loops.h>

#include <render/scene.h>
#include <util/util_thread.h>

PXR_NAMESPACE_USING_DIRECTIVE

HdCyclesResourceRegistry::HdCyclesResourceRegistry()
    : m_scene { nullptr }
{
}

std::vector<HdCyclesSceneBindingSharedPtr>
HdCyclesResourceRegistry::BindShaders(const std::vector<ccl::Shader*>& shaders)
{
    std::vector<HdCyclesSceneBindingSharedPtr> bindings;
    bindings.reserve(shaders.size());

    TfHash hasher;
    for (size_t i {}; i < shaders.size(); ++i) {
        ccl::Shader* shader = shaders[i];
        auto hash = hasher(shader);
        HdInstance<HdCyclesSceneBindingSharedPtr> instance = m_shader_bindings.GetInstance(hash);
        if (instance.IsFirstInstance()) {
            auto binding = std::make_shared<HdCyclesShaderBinding>(m_scene, shader);
            instance.SetValue(binding);
            bindings.push_back(binding);
        }
    }

    return bindings;
}

void
HdCyclesResourceRegistry::_Commit()
{
    ccl::thread_scoped_lock scene_lock { m_scene->mutex };

    // bind shaders
    for (auto& shader_binding : m_shader_bindings) {
        shader_binding.second.value->Bind();
    }
}

void
HdCyclesResourceRegistry::_GarbageCollect()
{
    ccl::thread_scoped_lock scene_lock { m_scene->mutex };

    // unbind unused shaders
    m_shader_bindings.GarbageCollect();
}
