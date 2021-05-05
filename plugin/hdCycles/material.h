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

#ifndef HD_CYCLES_MATERIAL_H
#define HD_CYCLES_MATERIAL_H

#include "api.h"

#include <pxr/imaging/hd/material.h>
#include <pxr/pxr.h>

namespace ccl {
class Object;
class Shader;
class ShaderGraph;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

// Terminal keys used in material networks.

// clang-format off
#define HD_CYCLES_MATERIAL_TERMINAL_TOKENS          \
    ((surface, "surface"))                          \
    ((cyclesSurface, "cycles:surface"))             \
    ((displacement, "displacement"))                \
    ((cyclesDisplacement, "cycles:displacement"))   \
    ((volume, "volume"))                            \
    ((cyclesVolume, "cycles:volume"))

TF_DECLARE_PUBLIC_TOKENS(HdCyclesMaterialTerminalTokens,
    HD_CYCLES_MATERIAL_TERMINAL_TOKENS
);
// clang-format on

class HdSceneDelegate;
class HdCyclesRenderDelegate;

/**
 * @brief HdCycles Material Sprim mapped to Cycles Material
 * 
 */
class HdCyclesMaterial final : public HdMaterial {
public:
    /**
     * @brief Construct a new HdCycles Material
     * 
     * @param id Path to the Material
     */
    HdCyclesMaterial(SdfPath const& id, HdCyclesRenderDelegate* a_renderDelegate);

    /**
     * @brief Destroy the HdCycles Material 
     * 
     */
    virtual ~HdCyclesMaterial();

    /**
     * @brief Pull invalidated material data and prepare/update the core Cycles 
     * representation.
     * 
     * This must be thread safe.
     * 
     * @param sceneDelegate The data source for the material
     * @param renderParam State
     * @param dirtyBits Which bits of scene data has changed
     */
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;

    /**
     * @brief Inform the scene graph which state needs to be downloaded in
     * the first Sync() call
     * 
     * @return The initial dirty state this material wants to query
     */
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /**
     * @brief Causes the shader to be reloaded
     * 
     */
    void Reload()
#if PXR_MAJOR_VERSION > 19
        override
#endif
        ;

    /**
     * @return Return true if this material is valid
     */
    bool IsValid() const;

    /**
     * @brief Return the static list of tokens supported.
     * 
     */
    static TfTokenVector const& GetShaderSourceTypes();

    /**
     * @brief Accessor for material's associated cycles shader
     * 
     * @return ccl::Shader* cycles shader 
     */
    ccl::Shader* GetCyclesShader() const;

protected:
    ccl::Shader* m_shader;
    ccl::ShaderGraph* m_shaderGraph;

    HdCyclesRenderDelegate* m_renderDelegate;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_MATERIAL_H
