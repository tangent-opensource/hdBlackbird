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

#include "light.h"

#include "renderParam.h"
#include "utils.h"

#include <render/graph.h>
#include <render/light.h>
#include <render/nodes.h>
#include <render/object.h>
#include <render/scene.h>
#include <render/shader.h>
#include <util/util_math_float3.h>
#include <util/util_string.h>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdLux/tokens.h>

#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

HdCyclesLight::HdCyclesLight(SdfPath const& id, TfToken const& lightType,
                             HdCyclesRenderDelegate* a_renderDelegate)
    : HdLight(id)
    , m_cyclesLight(nullptr)
    , m_cyclesShader(nullptr)
    , m_hdLightType(lightType)
    , m_backgroundNode(nullptr)
    , m_backgroundTransform(nullptr)
    , m_backgroundTexture(nullptr)
    , m_blackbodyNode(nullptr)
    , m_renderDelegate(a_renderDelegate)
{
    // Added to prevent fallback lights
    // TODO: Is this the best solution...
    if (id == SdfPath::EmptyPath())
        return;

    _CreateCyclesLight(m_renderDelegate->GetCyclesRenderParam());
}

HdCyclesLight::~HdCyclesLight()
{
    if (m_hdLightType == HdPrimTypeTokens->domeLight) {
        m_renderDelegate->GetCyclesRenderParam()->SetBackgroundShader(nullptr);
        m_renderDelegate->GetCyclesRenderParam()->Interrupt();
    }

    if (m_cyclesShader) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveShader(m_cyclesShader);
        delete m_cyclesShader;
    }

    if (m_cyclesLight) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveLight(m_cyclesLight);
        delete m_cyclesLight;
    }
}

void
HdCyclesLight::Finalize(HdRenderParam* renderParam)
{
}

void
HdCyclesLight::_CreateCyclesLight(HdCyclesRenderParam* renderParam)
{
    ccl::Scene* scene = renderParam->GetCyclesScene();
    m_cyclesLight     = new ccl::Light();

    m_cyclesShader        = new ccl::Shader();
    m_cyclesShader->graph = new ccl::ShaderGraph();

    m_cyclesLight->shader = m_cyclesShader;

    if (m_hdLightType == HdPrimTypeTokens->domeLight) {
        m_cyclesLight->type = ccl::LIGHT_BACKGROUND;

        m_backgroundNode        = new ccl::BackgroundNode();
        m_backgroundNode->color = ccl::make_float3(0.0f, 0.0f, 0.0f);

        m_backgroundNode->strength = 1.0f;
        m_cyclesShader->graph->add(m_backgroundNode);

        ccl::ShaderNode* out = m_cyclesShader->graph->output();
        m_cyclesShader->graph->connect(m_backgroundNode->output("Background"),
                                       out->input("Surface"));

        renderParam->SetBackgroundShader(m_cyclesShader);
    } else {
        m_emissionNode           = new ccl::EmissionNode();
        m_emissionNode->color    = ccl::make_float3(1.0f, 1.0f, 1.0f);
        m_emissionNode->strength = 1.0f;
        m_cyclesShader->graph->add(m_emissionNode);

        ccl::ShaderNode* out = m_cyclesShader->graph->output();
        m_cyclesShader->graph->connect(m_emissionNode->output("Emission"),
                                       out->input("Surface"));

        if (m_hdLightType == HdPrimTypeTokens->diskLight) {
            m_cyclesLight->type  = ccl::LIGHT_AREA;
            m_cyclesLight->round = true;

            m_cyclesLight->size = 1.0f;
        } else if (m_hdLightType == HdPrimTypeTokens->sphereLight) {
            m_cyclesLight->type = ccl::LIGHT_POINT;
        } else if (m_hdLightType == HdPrimTypeTokens->distantLight) {
            m_cyclesLight->type = ccl::LIGHT_DISTANT;
        } else if (m_hdLightType == HdPrimTypeTokens->rectLight) {
            m_cyclesLight->type  = ccl::LIGHT_AREA;
            m_cyclesLight->round = false;

            m_cyclesLight->size = 1.0f;
        }
    }

    renderParam->AddLight(m_cyclesLight);

    renderParam->AddShader(m_cyclesShader);

    // Set defaults
    m_cyclesLight->use_diffuse      = true;
    m_cyclesLight->use_glossy       = true;
    m_cyclesLight->use_transmission = true;
    m_cyclesLight->use_scatter      = true;
    m_cyclesLight->cast_shadow      = true;
    m_cyclesLight->use_mis          = true;
    m_cyclesLight->is_portal        = false;
    m_cyclesLight->max_bounces      = 1024;

    //TODO: Add support for random_id
    //m_cyclesLight->random_id = ...

    m_cyclesShader->tag_update(scene);
    m_cyclesLight->tag_update(scene);
}

void
HdCyclesLight::_SetTransform(const ccl::Transform& a_transform)
{
    if (!m_cyclesLight)
        return;

    m_cyclesLight->tfm = a_transform;

    if (m_cyclesLight->type == ccl::LIGHT_BACKGROUND) {
        if (m_backgroundTransform)
            m_backgroundTransform->ob_tfm = a_transform;
    } else {
        // Set the area light transforms
        m_cyclesLight->axisu = ccl::transform_get_column(&a_transform, 0);
        m_cyclesLight->axisv = ccl::transform_get_column(&a_transform, 1);
        m_cyclesLight->co    = ccl::transform_get_column(&a_transform, 3);
        m_cyclesLight->dir   = -ccl::transform_get_column(&a_transform, 2);
    }
}

void
HdCyclesLight::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
                    HdDirtyBits* dirtyBits)
{
    SdfPath id = GetId();

    HdCyclesRenderParam* param = (HdCyclesRenderParam*)renderParam;

    ccl::Scene* scene = param->GetCyclesScene();

    bool light_updated = false;

    if (*dirtyBits & HdLight::DirtyParams) {
        light_updated = true;

        // -- Common params

        // Color
        VtValue lightColor
            = sceneDelegate->GetLightParamValue(id, HdLightTokens->color);
        if (lightColor.IsHolding<GfVec3f>()) {
            GfVec3f v               = lightColor.UncheckedGet<GfVec3f>();
            m_cyclesLight->strength = ccl::make_float3(v[0], v[1], v[2]);
        }

        // Normalize
        VtValue normalize
            = sceneDelegate->GetLightParamValue(id, HdLightTokens->normalize);
        if (normalize.IsHolding<bool>()) {
            m_normalize = normalize.UncheckedGet<bool>();
        }


        // Exposure
        VtValue exposureValue
            = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure);

        float exposure = 1.0f;
        if (exposureValue.IsHolding<float>()) {
            exposure = powf(2.0f, exposureValue.UncheckedGet<float>());
        }

        // Intensity
        VtValue intensity
            = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity);
        if (intensity.IsHolding<float>()) {
            m_finalIntensity = intensity.UncheckedGet<float>() * exposure;
            m_cyclesLight->strength *= m_finalIntensity;
        }

        // TODO:
        // These two params have no direct mapping. Kept for future implementation

        // // Diffuse
        // VtValue diffuse
        //     = sceneDelegate->GetLightParamValue(id, HdLightTokens->diffuse);
        // if (diffuse.IsHolding<float>()) {
        //     m_cyclesLight->use_diffuse = (diffuse.UncheckedGet<float>()
        //                                   == 1.0f);
        // }

        // // Specular
        // VtValue specular
        //     = sceneDelegate->GetLightParamValue(id, HdLightTokens->specular);
        // if (specular.IsHolding<float>()) {
        //     m_cyclesLight->use_glossy = (specular.UncheckedGet<float>()
        //                                  == 1.0f);
        // }

        // Enable Temperature
        VtValue enableTemperature = sceneDelegate->GetLightParamValue(
            id, HdLightTokens->enableColorTemperature);
        if (enableTemperature.IsHolding<bool>()) {
            m_useTemperature = enableTemperature.UncheckedGet<bool>();
        }

        if (m_useTemperature) {
            // Get Temperature
            VtValue temperature = sceneDelegate->GetLightParamValue(
                id, HdLightTokens->colorTemperature);
            if (temperature.IsHolding<float>()) {
                m_temperature = temperature.UncheckedGet<float>();

                // Add temperature node
                if (!m_blackbodyNode) {
                    m_blackbodyNode = new ccl::BlackbodyNode();

                    m_cyclesShader->graph->add(m_blackbodyNode);

                    m_cyclesShader->graph->connect(
                        m_blackbodyNode->output("Color"),
                        m_emissionNode->input("Color"));
                }

                m_blackbodyNode->temperature = m_temperature;
                m_cyclesShader->tag_update(scene);
            }
        }

        if (m_hdLightType == HdPrimTypeTokens->rectLight) {
            m_cyclesLight->axisu
                = ccl::transform_get_column(&m_cyclesLight->tfm, 0);
            m_cyclesLight->axisv
                = ccl::transform_get_column(&m_cyclesLight->tfm, 1);

            VtValue width
                = sceneDelegate->GetLightParamValue(id, HdLightTokens->width);
            if (width.IsHolding<float>())
                m_cyclesLight->sizeu = width.UncheckedGet<float>();

            VtValue height
                = sceneDelegate->GetLightParamValue(id, HdLightTokens->height);
            if (height.IsHolding<float>())
                m_cyclesLight->sizev = height.UncheckedGet<float>();
        }

        if (m_hdLightType == HdPrimTypeTokens->diskLight) {
            // TODO:
            // Disk Lights cannot be ovals, but Blender can export oval lights...
            // This will be fixed in the great light transition when the new light API
            // is released

            // VtValue width = sceneDelegate->GetLightParamValue(id, HdLightTokens->width);
            // if(width.IsHolding<float>())
            //     m_cyclesLight->sizeu = width.UncheckedGet<float>();

            // VtValue height = sceneDelegate->GetLightParamValue(id, HdLightTokens->height);
            // if(height.IsHolding<float>())
            //     m_cyclesLight->sizev = height.UncheckedGet<float>();

            m_cyclesLight->axisu
                = ccl::transform_get_column(&m_cyclesLight->tfm, 0);
            m_cyclesLight->axisv
                = ccl::transform_get_column(&m_cyclesLight->tfm, 1);

            VtValue radius
                = sceneDelegate->GetLightParamValue(id, HdLightTokens->radius);
            if (radius.IsHolding<float>()) {
                m_cyclesLight->sizeu = radius.UncheckedGet<float>() * 2.0f;
                m_cyclesLight->sizev = radius.UncheckedGet<float>() * 2.0f;
            }
        }

        if (m_hdLightType == HdPrimTypeTokens->cylinderLight) {
            // TODO: Implement
            // Cycles has no concept of cylinder lights.
        }

        if (m_hdLightType == HdPrimTypeTokens->sphereLight) {
            VtValue radius
                = sceneDelegate->GetLightParamValue(id, HdLightTokens->radius);
            if (radius.IsHolding<float>())
                m_cyclesLight->size = radius.UncheckedGet<float>();

            //Spot shaping
            VtValue shapingConeAngle = sceneDelegate->GetLightParamValue(
                id, HdLightTokens->shapingConeAngle);
            if (shapingConeAngle.IsHolding<float>()) {
                m_cyclesLight->spot_angle
                    = shapingConeAngle.UncheckedGet<float>()
                      * ((float)M_PI / 180.0f) * 2.0f;
                m_cyclesLight->type = ccl::LIGHT_SPOT;
            }

            VtValue shapingConeSoftness = sceneDelegate->GetLightParamValue(
                id, HdLightTokens->shapingConeSoftness);
            if (shapingConeSoftness.IsHolding<float>()) {
                m_cyclesLight->spot_smooth
                    = shapingConeSoftness.UncheckedGet<float>();
                m_cyclesLight->type = ccl::LIGHT_SPOT;
            }
        }

        if (m_hdLightType == HdPrimTypeTokens->distantLight) {
            // TODO: Test this
            VtValue angle
                = sceneDelegate->GetLightParamValue(id, HdLightTokens->angle);
            if (angle.IsHolding<float>())
                m_cyclesLight->angle = angle.UncheckedGet<float>();
        }

        if (m_hdLightType == HdPrimTypeTokens->domeLight) {
            m_backgroundNode->color = m_cyclesLight->strength;

            m_backgroundNode->strength = m_finalIntensity;
            VtValue textureFile
                = sceneDelegate->GetLightParamValue(id,
                                                    HdLightTokens->textureFile);

            if (textureFile.IsHolding<SdfAssetPath>()) {
                SdfAssetPath ap      = textureFile.UncheckedGet<SdfAssetPath>();
                std::string filepath = ap.GetResolvedPath();

                // TODO: Prevent this string comparison
                if (filepath != "" && m_backgroundFilePath != filepath) {
                    if (m_backgroundTexture == nullptr) {
                        // Add environment texture nodes
                        m_backgroundTransform = new ccl::TextureCoordinateNode();
                        m_backgroundTransform->use_transform = true;
                        m_backgroundTransform->ob_tfm = m_cyclesLight->tfm;

                        m_cyclesShader->graph->add(m_backgroundTransform);

                        m_backgroundTexture = new ccl::EnvironmentTextureNode();

                        m_cyclesShader->graph->add(m_backgroundTexture);

                        m_cyclesShader->graph->connect(
                            m_backgroundTransform->output("Object"),
                            m_backgroundTexture->input("Vector"));
                        m_cyclesShader->graph->connect(
                            m_backgroundTexture->output("Color"),
                            m_backgroundNode->input("Color"));
                    }

                    m_backgroundTexture->filename = filepath;
                    m_backgroundFilePath          = filepath;

                    m_cyclesShader->tag_update(scene);
                }
            }
        }
    }

#ifdef USE_USD_CYCLES_SCHEMA

    m_cyclesLight->cast_shadow
        = _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightCast_shadow,
                                       m_cyclesLight->cast_shadow);

    m_cyclesLight->use_diffuse
        = _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightUse_diffuse,
                                       m_cyclesLight->use_diffuse);

    m_cyclesLight->use_glossy
        = _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightUse_glossy,
                                       m_cyclesLight->use_glossy);

    m_cyclesLight->use_transmission = _HdCyclesGetLightParam<bool>(
        id, sceneDelegate, usdCyclesTokens->cyclesLightUse_transmission,
        m_cyclesLight->use_transmission);

    m_cyclesLight->use_scatter
        = _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightUse_scatter,
                                       m_cyclesLight->use_scatter);

    m_cyclesLight->use_mis
        = _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightUse_mis,
                                       m_cyclesLight->use_mis);

    m_cyclesLight->is_portal
        = _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightIs_portal,
                                       m_cyclesLight->is_portal);

    m_cyclesLight->max_bounces
        = _HdCyclesGetLightParam<int>(id, sceneDelegate,
                                      usdCyclesTokens->cyclesLightMax_bounces,
                                      m_cyclesLight->max_bounces);

#endif


    // TODO: Light is_enabled doesn't seem to have any effect
    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        light_updated             = true;
        m_cyclesLight->is_enabled = sceneDelegate->GetVisible(id);
    }

    if (*dirtyBits & HdLight::DirtyTransform) {
        light_updated = true;
        _SetTransform(HdCyclesExtractTransform(sceneDelegate, id));
    }

    if (light_updated) {
        m_cyclesShader->tag_update(param->GetCyclesScene());
        m_cyclesLight->tag_update(scene);

        param->Interrupt();
    }

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
HdCyclesLight::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllDirty | HdLight::DirtyParams
           | HdLight::DirtyTransform;
}

bool
HdCyclesLight::IsValid() const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE