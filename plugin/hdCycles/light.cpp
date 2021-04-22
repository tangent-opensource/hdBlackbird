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

#include <render/object.h>
#include <render/scene.h>
#include <render/shader.h>
#include <util/util_hash.h>
#include <util/util_math_float3.h>
#include <util/util_string.h>
#include <util/util_transform.h>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdLux/tokens.h>

#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

HdCyclesLight::HdCyclesLight(SdfPath const& id, TfToken const& lightType, HdCyclesRenderDelegate* a_renderDelegate)
    : HdLight(id)
    , m_hdLightType(lightType)
    , m_cyclesLight(nullptr)
    , m_shaderGraphBits(ShaderGraphBits::Default)
    , m_renderDelegate(a_renderDelegate)
    , m_finalIntensity(1.0f)
{
    // Added to prevent fallback lights
    // TODO: Is this the best solution...
    if (id == SdfPath::EmptyPath())
        return;

    _CreateCyclesLight(id, m_renderDelegate->GetCyclesRenderParam());
}

HdCyclesLight::~HdCyclesLight()
{
    if (m_hdLightType == HdPrimTypeTokens->domeLight) {
        m_renderDelegate->GetCyclesRenderParam()->Interrupt();
    }

    if (m_cyclesLight) {
        if (m_cyclesLight->get_shader()) {
            m_renderDelegate->GetCyclesRenderParam()->RemoveShader(m_cyclesLight->get_shader());
            delete m_cyclesLight->get_shader();
        }
        m_renderDelegate->GetCyclesRenderParam()->RemoveLight(m_cyclesLight);
        delete m_cyclesLight;
    }
}

void
HdCyclesLight::Finalize(HdRenderParam* renderParam)
{
}

void
HdCyclesLight::_CreateCyclesLight(SdfPath const& id, HdCyclesRenderParam* renderParam)
{
    ccl::Scene* scene = renderParam->GetCyclesScene();
    m_cyclesLight     = new ccl::Light();

    m_cyclesLight->name = ccl::ustring(id.GetName().c_str());

    ccl::Shader *shader = new ccl::Shader();

    m_cyclesLight->set_shader(shader);

    if (m_hdLightType == HdPrimTypeTokens->domeLight) {
        m_cyclesLight->set_light_type(ccl::LIGHT_BACKGROUND);
        shader->set_graph(_GetDefaultShaderGraph(true));
        renderParam->SetBackgroundShader(shader);
    } else {
        if (m_hdLightType == HdPrimTypeTokens->diskLight) {
            m_cyclesLight->set_light_type(ccl::LIGHT_AREA);
            m_cyclesLight->set_round(true);

            m_cyclesLight->set_size(1.0f);
        } else if (m_hdLightType == HdPrimTypeTokens->sphereLight) {
            m_cyclesLight->set_light_type(ccl::LIGHT_POINT);
        } else if (m_hdLightType == HdPrimTypeTokens->distantLight) {
            m_cyclesLight->set_light_type(ccl::LIGHT_DISTANT);
        } else if (m_hdLightType == HdPrimTypeTokens->rectLight) {
            m_cyclesLight->set_light_type(ccl::LIGHT_AREA);
            m_cyclesLight->set_round(false);

            m_cyclesLight->set_size(1.0f);
        }

        shader->set_graph(_GetDefaultShaderGraph());
    }

    renderParam->AddLight(m_cyclesLight);

    renderParam->AddShader(shader);

    // Set defaults
    m_cyclesLight->set_use_diffuse(true);
    m_cyclesLight->set_use_glossy(true);
    m_cyclesLight->set_use_transmission(true);
    m_cyclesLight->set_use_scatter(true);
    m_cyclesLight->set_cast_shadow(true);
    m_cyclesLight->set_use_mis(true);
    m_cyclesLight->set_is_portal(false);
    m_cyclesLight->set_max_bounces(1024);

    m_cyclesLight->set_random_id(
        ccl::hash_uint2(ccl::hash_string(m_cyclesLight->name.c_str()), 0));

    shader->tag_update(scene);
    m_cyclesLight->tag_update(scene);
}

void
HdCyclesLight::_SetTransform(const ccl::Transform& a_transform)
{
    if (!m_cyclesLight)
        return;

    m_cyclesLight->set_tfm(a_transform);

    if (m_cyclesLight->get_light_type() == ccl::LIGHT_BACKGROUND) {
        ccl::TextureCoordinateNode *backgroundTransform = 
            (ccl::TextureCoordinateNode*)_FindShaderNode(
            m_cyclesLight->get_shader()->graph, ccl::TextureCoordinateNode::node_type);
        if (backgroundTransform)
            backgroundTransform->set_ob_tfm(a_transform);
    } else {
        // Set the area light transforms
        m_cyclesLight->set_axisu(ccl::transform_get_column(&a_transform, 0));
        m_cyclesLight->set_axisv(ccl::transform_get_column(&a_transform, 1));
        m_cyclesLight->set_co(ccl::transform_get_column(&a_transform, 3));
        m_cyclesLight->set_dir(ccl::transform_get_column(&a_transform, 2));
    }
}

ccl::ShaderGraph*
HdCyclesLight::_GetDefaultShaderGraph(bool isBackground)
{
    ccl::ShaderGraph *graph = new ccl::ShaderGraph();
    if (isBackground) {
        ccl::BackgroundNode *backgroundNode = new ccl::BackgroundNode();
        backgroundNode->set_color(ccl::make_float3(0.0f, 0.0f, 0.0f));

        backgroundNode->set_strength(1.0f);
        graph->add(backgroundNode);

        ccl::ShaderNode* out = graph->output();
        graph->connect(backgroundNode->output("Background"), out->input("Surface"));
    } else {
        ccl::EmissionNode *emissionNode = new ccl::EmissionNode();
        emissionNode->set_color(ccl::make_float3(1.0f, 1.0f, 1.0f));
        emissionNode->set_strength(1.0f);
        graph->add(emissionNode);

        ccl::ShaderNode* out = graph->output();
        graph->connect(emissionNode->output("Emission"), out->input("Surface"));
    }
    return graph;
}

ccl::ShaderNode*
HdCyclesLight::_FindShaderNode(const ccl::ShaderGraph *graph, const ccl::NodeType *type)
{
    for( ccl::ShaderNode *node : graph->nodes ) {
        if (node->type == type) {
            return node;
        }
    }
    return nullptr;
}

void
HdCyclesLight::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    SdfPath id = GetId();

    HdCyclesRenderParam* param = static_cast<HdCyclesRenderParam*>(renderParam);

    ccl::Scene* scene = param->GetCyclesScene();

    bool light_updated = false;

    if (*dirtyBits & HdLight::DirtyParams) {
        light_updated = true;
        ccl::ShaderGraph *oldGraph = m_cyclesLight->get_shader()->graph;

        // Check if we need to rebuild the graph
        ShaderGraphBits shaderGraphBits = ShaderGraphBits::Default;

        VtValue enableTemperature = sceneDelegate->GetLightParamValue(id, HdLightTokens->enableColorTemperature);
        if (enableTemperature.IsHolding<bool>()) {
            shaderGraphBits = enableTemperature.UncheckedGet<bool>()
                                  ? (ShaderGraphBits)(shaderGraphBits | ShaderGraphBits::Temperature)
                                  : shaderGraphBits;
        }

        VtValue iesFile = sceneDelegate->GetLightParamValue(id, HdLightTokens->shapingIesFile);
        if (iesFile.IsHolding<SdfAssetPath>()) {
            shaderGraphBits = (ShaderGraphBits)(shaderGraphBits|ShaderGraphBits::IES);
        }

        VtValue textureFile = sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile);
        if (textureFile.IsHolding<SdfAssetPath>()) {
            SdfAssetPath ap      = textureFile.UncheckedGet<SdfAssetPath>();
            std::string filepath = ap.GetResolvedPath();

            if (filepath.length() > 0) {
                shaderGraphBits = (ShaderGraphBits)(shaderGraphBits|ShaderGraphBits::Texture);
            }
        }

        ccl::ShaderGraph *graph = nullptr;
        ccl::ShaderNode *outNode = nullptr;

        // Ideally we would just check if it is different, however some nodes
        // simplify & fold internally so we have to re-create the graph, so if
        // there is any nodes used, re-create...
        if (shaderGraphBits || 
            shaderGraphBits != m_shaderGraphBits) {
            graph = _GetDefaultShaderGraph(m_cyclesLight->get_light_type() == ccl::LIGHT_BACKGROUND);
            outNode = graph->output()->input("Surface")->link->parent;
            m_shaderGraphBits = shaderGraphBits;
        } else {
            outNode = oldGraph->output()->input("Surface")->link->parent;
        }

        // -- Common params

        // Color
        VtValue lightColor = sceneDelegate->GetLightParamValue(id, HdLightTokens->color);
        if (lightColor.IsHolding<GfVec3f>()) {
            GfVec3f v               = lightColor.UncheckedGet<GfVec3f>();
            m_cyclesLight->set_strength(ccl::make_float3(v[0], v[1], v[2]));
        }

        // Normalize
        VtValue normalize = sceneDelegate->GetLightParamValue(id, HdLightTokens->normalize);
        if (normalize.IsHolding<bool>()) {
            m_normalize = normalize.UncheckedGet<bool>();
        }


        // Exposure
        VtValue exposureValue = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure);

        float exposure = 1.0f;
        if (exposureValue.IsHolding<float>()) {
            exposure = powf(2.0f, exposureValue.UncheckedGet<float>());
        }

        // Intensity
        VtValue intensity = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity);
        if (intensity.IsHolding<float>()) {
            m_finalIntensity = intensity.UncheckedGet<float>() * exposure;
            m_cyclesLight->set_strength(m_cyclesLight->get_strength() * m_finalIntensity);
        }

        // Light cast shadow
        m_cyclesLight->set_cast_shadow(
            _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                           HdLightTokens->shadowEnable,
                                           true));

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
        ccl::BlackbodyNode *blackbodyNode = nullptr;
        if (shaderGraphBits & ShaderGraphBits::Temperature) {
            // Get Temperature
            VtValue temperature = sceneDelegate->GetLightParamValue(id, HdLightTokens->colorTemperature);
            if (temperature.IsHolding<float>()) {
                if (graph) {
                    blackbodyNode = new ccl::BlackbodyNode();
                    graph->add(blackbodyNode);

                    graph->connect(blackbodyNode->output("Color"), outNode->input("Color"));
                } else {
                    blackbodyNode = (ccl::BlackbodyNode*)_FindShaderNode(oldGraph, ccl::BlackbodyNode::node_type);
                }
                assert(blackbodyNode != nullptr);
                blackbodyNode->set_temperature(temperature.UncheckedGet<float>());
            }
        }

        // Enable IES profile. Angle scale and normalize are not supported currently.
        // TODO: Perhaps usdCycles could store embedded IES into a string? ->ies can
        // be used instead of ->filename, Blender uses it to store IES profiles in
        // .blend files...
        if (shaderGraphBits & ShaderGraphBits::IES) {
            SdfAssetPath ap         = iesFile.UncheckedGet<SdfAssetPath>();
            std::string iesfilepath = ap.GetResolvedPath();

            ccl::IESLightNode *iesNode = nullptr;
            if (graph) {
                ccl::TextureCoordinateNode *iesTransform = 
                    new ccl::TextureCoordinateNode();
                iesTransform->set_use_transform(true);
                iesTransform->set_ob_tfm(m_cyclesLight->get_tfm());
                graph->add(iesTransform);

                iesNode = new ccl::IESLightNode();
                graph->add(iesNode);

                graph->connect(iesTransform->output("Normal"), iesNode->input("Vector"));

                graph->connect(iesNode->output("Fac"), outNode->input("Strength"));
            } else {
                iesNode = (ccl::IESLightNode*)_FindShaderNode(oldGraph, ccl::IESLightNode::node_type);
            }
            assert(iesNode != nullptr);
            iesNode->set_filename(ccl::ustring(iesfilepath.c_str()));
        }

        if (m_hdLightType == HdPrimTypeTokens->rectLight) {
            m_cyclesLight->set_axisu(
                ccl::transform_get_column(&m_cyclesLight->get_tfm(), 0));
            m_cyclesLight->set_axisv(
                ccl::transform_get_column(&m_cyclesLight->get_tfm(), 1));

            VtValue width = sceneDelegate->GetLightParamValue(id, HdLightTokens->width);
            if (width.IsHolding<float>())
                m_cyclesLight->set_sizeu(width.UncheckedGet<float>());

            VtValue height = sceneDelegate->GetLightParamValue(id, HdLightTokens->height);
            if (height.IsHolding<float>())
                m_cyclesLight->set_sizev(height.UncheckedGet<float>());

            if (shaderGraphBits & ShaderGraphBits::Texture) {
                SdfAssetPath ap      = textureFile.UncheckedGet<SdfAssetPath>();
                std::string filepath = ap.GetResolvedPath();

                ccl::ImageTextureNode *textureNode = nullptr;
                if (graph) {
                    textureNode = new ccl::ImageTextureNode();
                    graph->add(textureNode);
                    ccl::GeometryNode *geometryNode = new ccl::GeometryNode();
                    graph->add(geometryNode);

                    graph->connect(geometryNode->output("Parametric"), textureNode->input("Vector"));

                    if ((shaderGraphBits & ShaderGraphBits::Temperature) && blackbodyNode) {
                        ccl::VectorMathNode *vecMathNode = new ccl::VectorMathNode();
                        vecMathNode->set_math_type(ccl::NODE_VECTOR_MATH_MULTIPLY);
                        graph->add(vecMathNode);

                        graph->connect(textureNode->output("Color"), vecMathNode->input("Vector1"));

                        graph->connect(blackbodyNode->output("Color"), vecMathNode->input("Vector2"));

                        graph->disconnect(outNode->input("Color"));
                        graph->connect(vecMathNode->output("Vector"), outNode->input("Color"));
                    } else {
                        graph->connect(textureNode->output("Color"), outNode->input("Color"));
                    }
                } else {
                    textureNode = (ccl::ImageTextureNode*)_FindShaderNode(oldGraph, ccl::ImageTextureNode::node_type);
                }
                assert(textureNode != nullptr);
                textureNode->set_filename(ccl::ustring(filepath.c_str()));
            }
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

            m_cyclesLight->set_axisu(
                ccl::transform_get_column(&m_cyclesLight->get_tfm(), 0));
            m_cyclesLight->set_axisv(
                ccl::transform_get_column(&m_cyclesLight->get_tfm(), 1));

            VtValue radius = sceneDelegate->GetLightParamValue(id, HdLightTokens->radius);
            if (radius.IsHolding<float>()) {
                m_cyclesLight->set_sizeu(radius.UncheckedGet<float>() * 2.0f);
                m_cyclesLight->set_sizev(radius.UncheckedGet<float>() * 2.0f);
            }
        }

        if (m_hdLightType == HdPrimTypeTokens->cylinderLight) {
            // TODO: Implement
            // Cycles has no concept of cylinder lights.
        }

        if (m_hdLightType == HdPrimTypeTokens->sphereLight) {
            VtValue radius = sceneDelegate->GetLightParamValue(id, HdLightTokens->radius);
            if (radius.IsHolding<float>())
                m_cyclesLight->set_size(radius.UncheckedGet<float>());

            //Spot shaping
            VtValue shapingConeAngle = sceneDelegate->GetLightParamValue(id, HdLightTokens->shapingConeAngle);
            if (shapingConeAngle.IsHolding<float>()) {
                m_cyclesLight->set_spot_angle(
                    shapingConeAngle.UncheckedGet<float>()
                      * ((float)M_PI / 180.0f) * 2.0f);
                m_cyclesLight->set_light_type(ccl::LIGHT_SPOT);
            }

            VtValue shapingConeSoftness = sceneDelegate->GetLightParamValue(id, HdLightTokens->shapingConeSoftness);
            if (shapingConeSoftness.IsHolding<float>()) {
                m_cyclesLight->set_spot_smooth(
                    shapingConeSoftness.UncheckedGet<float>());
                m_cyclesLight->set_light_type(ccl::LIGHT_SPOT);
            }
        }

        if (m_hdLightType == HdPrimTypeTokens->distantLight) {
            // TODO: Test this
            VtValue angle = sceneDelegate->GetLightParamValue(id, HdLightTokens->angle);
            if (angle.IsHolding<float>())
                m_cyclesLight->set_angle(angle.UncheckedGet<float>());
        }

        if (m_hdLightType == HdPrimTypeTokens->domeLight) {
            ccl::BackgroundNode *backroundNode = (ccl::BackgroundNode*)outNode;
            backroundNode->set_color(m_cyclesLight->get_strength());

            backroundNode->set_strength(m_finalIntensity);

            if (shaderGraphBits & ShaderGraphBits::Texture) {
                SdfAssetPath ap      = textureFile.UncheckedGet<SdfAssetPath>();
                std::string filepath = ap.GetResolvedPath();

                ccl::EnvironmentTextureNode *backgroundTexture = nullptr;
                if (graph) {
                    // Add environment texture nodes
                    ccl::TextureCoordinateNode *backgroundTransform = 
                        new ccl::TextureCoordinateNode();
                    backgroundTransform->set_use_transform(true);
                    backgroundTransform->set_ob_tfm(m_cyclesLight->get_tfm());
                    graph->add(backgroundTransform);

                    backgroundTexture = new ccl::EnvironmentTextureNode();
                    if (param->GetUpAxis() == HdCyclesRenderParam::UpAxis::Y) {
                        // Change co-ordinate mapping on environment texture to match other Hydra delegates
                        backgroundTexture->tex_mapping.y_mapping = ccl::TextureMapping::Z;
                        backgroundTexture->tex_mapping.z_mapping = ccl::TextureMapping::Y;
                        backgroundTexture->tex_mapping.scale     = ccl::make_float3(-1.0f, 1.0f, 1.0f);
                        backgroundTexture->tex_mapping.rotation  = ccl::make_float3(0.0f, 0.0f, M_PI_F * -0.5f);
                    }

                    graph->add(backgroundTexture);

                    graph->connect(backgroundTransform->output("Object"), backgroundTexture->input("Vector"));

                    if ((shaderGraphBits & ShaderGraphBits::Temperature) && blackbodyNode) {
                        ccl::VectorMathNode *vecMathNode = new ccl::VectorMathNode();
                        vecMathNode->set_math_type(ccl::NODE_VECTOR_MATH_MULTIPLY);
                        graph->add(vecMathNode);

                        graph->connect(backgroundTexture->output("Color"), vecMathNode->input("Vector1"));

                        graph->connect(blackbodyNode->output("Color"), vecMathNode->input("Vector2"));

                        graph->disconnect(outNode->input("Color"));
                        graph->connect(vecMathNode->output("Vector"), outNode->input("Color"));
                    } else {
                        graph->connect(backgroundTexture->output("Color"), outNode->input("Color"));
                    }
                } else {
                    backgroundTexture
                        = (ccl::EnvironmentTextureNode*)_FindShaderNode(oldGraph,
                                                                        ccl::EnvironmentTextureNode::node_type);
                }
                assert(backgroundTexture != nullptr);
                backgroundTexture->set_filename(ccl::ustring(filepath.c_str()));
            }
        }

        if (graph) {
            m_cyclesLight->get_shader()->set_graph(graph);
        }
    }

#ifdef USE_USD_CYCLES_SCHEMA

    m_cyclesLight->set_use_diffuse(
        _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightUse_diffuse,
                                       m_cyclesLight->get_use_diffuse()));

    m_cyclesLight->set_use_glossy(
        _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightUse_glossy,
                                       m_cyclesLight->get_use_glossy()));

    m_cyclesLight->set_use_transmission(_HdCyclesGetLightParam<bool>(
        id, sceneDelegate, usdCyclesTokens->cyclesLightUse_transmission,
        m_cyclesLight->get_use_transmission()));

    m_cyclesLight->set_use_scatter(
        _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightUse_scatter,
                                       m_cyclesLight->get_use_scatter()));

    m_cyclesLight->set_use_mis(
        _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightUse_mis,
                                       m_cyclesLight->get_use_mis()));

    m_cyclesLight->set_is_portal(
        _HdCyclesGetLightParam<bool>(id, sceneDelegate,
                                       usdCyclesTokens->cyclesLightIs_portal,
                                       m_cyclesLight->get_is_portal()));

    m_cyclesLight->set_samples(
        _HdCyclesGetLightParam<int>(id, sceneDelegate,
                                      usdCyclesTokens->cyclesLightSamples,
                                      m_cyclesLight->get_samples()));

    m_cyclesLight->set_max_bounces(
        _HdCyclesGetLightParam<int>(id, sceneDelegate,
                                      usdCyclesTokens->cyclesLightMax_bounces,
                                      m_cyclesLight->get_max_bounces()));

#endif


    // TODO: Light is_enabled doesn't seem to have any effect
    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        light_updated             = true;
        m_cyclesLight->set_is_enabled(sceneDelegate->GetVisible(id));
    }

    if (*dirtyBits & HdLight::DirtyTransform) {
        light_updated = true;
        _SetTransform(HdCyclesExtractTransform(sceneDelegate, id));
    }

    if (light_updated) {
        m_cyclesLight->get_shader()->tag_update(scene);
        m_cyclesLight->tag_update(scene);

        param->Interrupt();
    }

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
HdCyclesLight::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllDirty | HdLight::DirtyParams | HdLight::DirtyTransform;
}

bool
HdCyclesLight::IsValid() const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE