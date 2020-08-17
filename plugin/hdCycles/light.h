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

#ifndef HD_CYCLES_LIGHT_H
#define HD_CYCLES_LIGHT_H

#include "api.h"

#include "renderDelegate.h"

#include <util/util_transform.h>

#include <pxr/imaging/hd/light.h>
#include <pxr/pxr.h>

namespace ccl {
class Light;
class Shader;
class Scene;
class BackgroundNode;
class TextureCoordinateNode;
class EnvironmentTextureNode;
class EmissionNode;
class BlackbodyNode;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;
class HdCyclesRenderParam;
class HdCyclesRenderDelegate;

/**
 * @brief Cycles Light Sprim mapped to Cycles Light
 * More work will be done here when the new light node network schema is released.
 * DomeLights/WorldMaterial is currently pretty hard coded, this will also be
 * unecessary with the new changes.
 *
 */
class HdCyclesLight final : public HdLight {
public:
    /**
     * @brief Construct a new HdCycles Light object
     *
     * @param id Path to the Light Primitive
     * @param lightType Type of light to create
     * @param a_renderDelegate Associated Render Delegate
     * as a prototype
     */
    HdCyclesLight(SdfPath const& id, TfToken const& lightType,
                  HdCyclesRenderDelegate* a_renderDelegate);

    /**
     * @brief Destroy the HdCycles Light object
     *
     */
    virtual ~HdCyclesLight();

    /**
     * @brief Pull invalidated light data and prepare/update the core Cycles
     * representation.
     *
     * This must be thread safe.
     *
     * @param sceneDelegate The data source for the light
     * @param renderParam State
     * @param dirtyBits Which bits of scene data has changed
     */
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    /**
     * @brief Inform the scene graph which state needs to be downloaded in
     * the first Sync() call
     *
     * @return The initial dirty state this light wants to query
     */
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /**
     * @return Return true if this light is valid.
     */
    bool IsValid() const;

    /**
     * @brief TODO: Implement
     */
    void Finalize(HdRenderParam* renderParam) override;

private:
    /**
     * @brief Create the cycles light representation
     *
     * @param scene Cycles scene to add light to
     * @param transform Initial transform for object
     * @return New allocated pointer to ccl::Light
     */
    void _CreateCyclesLight(HdCyclesRenderParam* renderParam);

    /**
     * @brief Set transform of light and associate light types
     * 
     * @param a_transform Transform to use
     */
    void _SetTransform(const ccl::Transform& a_transform);

    const TfToken m_hdLightType;
    ccl::Light* m_cyclesLight;
    ccl::Shader* m_cyclesShader;
    ccl::EmissionNode* m_emissionNode;

    // Background light specifics
    ccl::BackgroundNode* m_backgroundNode;
    ccl::TextureCoordinateNode* m_backgroundTransform;
    ccl::EnvironmentTextureNode* m_backgroundTexture;
    ccl::BlackbodyNode* m_blackbodyNode;
    std::string m_backgroundFilePath;

    bool m_normalize;
    bool m_useTemperature;
    float m_temperature;

    HdCyclesRenderDelegate* m_renderDelegate;

    float m_finalIntensity;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_LIGHT_H
