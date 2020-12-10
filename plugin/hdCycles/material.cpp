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

#include "material.h"

#include "renderDelegate.h"
#include "renderParam.h"
#include "utils.h"

#include <render/nodes.h>
#include <render/object.h>
#include <render/shader.h>
#include <util/util_math_float3.h>
#include <util/util_string.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hf/diagnostic.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (PxrDisplace)
    (bxdf)
    (OSL)
    (diffuseColor)
    (roughness)
    (metallic)
    (specular)
    (file)
    (varname)
    (Color)
    (rgb)
    (r)
    (g)
    (b)
    (st)
    (Vector)
    (base_color)
    (result)
    (UV)
);
// clang-format on

TF_DEFINE_PUBLIC_TOKENS(HdCyclesMaterialTerminalTokens,
                        HD_CYCLES_MATERIAL_TERMINAL_TOKENS);

TF_MAKE_STATIC_DATA(NdrTokenVec, _sourceTypes)
{
    *_sourceTypes = { TfToken("OSL"), TfToken("cycles") };
}

#ifdef USE_USD_CYCLES_SCHEMA

std::map<TfToken, ccl::DisplacementMethod> DISPLACEMENT_CONVERSION = {
    { usdCyclesTokens->displacement_bump, ccl::DISPLACE_BUMP },
    { usdCyclesTokens->displacement_true, ccl::DISPLACE_TRUE },
    { usdCyclesTokens->displacement_both, ccl::DISPLACE_BOTH },
};

std::map<TfToken, ccl::VolumeInterpolation> VOLUME_INTERPOLATION_CONVERSION = {
    { usdCyclesTokens->volume_interpolation_linear,
      ccl::VOLUME_INTERPOLATION_LINEAR },
    { usdCyclesTokens->volume_interpolation_cubic,
      ccl::VOLUME_INTERPOLATION_CUBIC },
};

std::map<TfToken, ccl::VolumeSampling> VOLUME_SAMPLING_CONVERSION = {
    { usdCyclesTokens->volume_sampling_distance, ccl::VOLUME_SAMPLING_DISTANCE },
    { usdCyclesTokens->volume_sampling_equiangular,
      ccl::VOLUME_SAMPLING_EQUIANGULAR },
    { usdCyclesTokens->volume_sampling_multiple_importance,
      ccl::VOLUME_SAMPLING_MULTIPLE_IMPORTANCE },
};

#endif

bool
IsValidCyclesIdentifier(const std::string& identifier)
{
    bool isvalid = identifier.rfind("cycles_") == 0;

    // DEPRECATED:
    // Only needed for retroactive support of pre 0.8.0 cycles shaders
    isvalid += identifier.rfind("cycles:") == 0;

    return isvalid;
}

TfTokenVector const&
HdCyclesMaterial::GetShaderSourceTypes()
{
    return *_sourceTypes;
}

HdCyclesMaterial::HdCyclesMaterial(SdfPath const& id,
                                   HdCyclesRenderDelegate* a_renderDelegate)
    : HdMaterial(id)
    , m_shader(nullptr)
    , m_shaderGraph(nullptr)
    , m_renderDelegate(a_renderDelegate)
{
    m_shader        = new ccl::Shader();
    m_shader->name  = id.GetString();
    m_shaderGraph   = new ccl::ShaderGraph();
    m_shader->graph = m_shaderGraph;

    if (m_renderDelegate)
        m_renderDelegate->GetCyclesRenderParam()->AddShader(m_shader);
}

HdCyclesMaterial::~HdCyclesMaterial()
{
    if (m_shader) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveShader(m_shader);
        delete m_shader;
    }
}

// TODO: These conversion functions will be moved to a more generic
// Material Adapter...

ccl::ShaderNode*
matConvertUSDPrimvarReader(HdMaterialNode& usd_node,
                           ccl::ShaderGraph* cycles_shader_graph)
{
    ccl::UVMapNode* uvmap = new ccl::UVMapNode();
    uvmap->attribute      = ccl::ustring("st");

    for (std::pair<TfToken, VtValue> params : usd_node.parameters) {
        if (params.first == _tokens->varname) {
            if (params.second.IsHolding<TfToken>()) {
                uvmap->attribute = ccl::ustring(
                    params.second.Get<TfToken>().GetString().c_str());
            }
        }
    }
    cycles_shader_graph->add(uvmap);
    return uvmap;
}

ccl::ShaderNode*
matConvertUSDUVTexture(HdMaterialNode& usd_node,
                       ccl::ShaderGraph* cycles_shader_graph)
{
    ccl::ImageTextureNode* imageTexture = new ccl::ImageTextureNode();

    for (std::pair<TfToken, VtValue> params : usd_node.parameters) {
        if (params.first == _tokens->file) {
            if (params.second.IsHolding<SdfAssetPath>()) {
                imageTexture->filename = ccl::ustring(
                    params.second.Get<SdfAssetPath>().GetResolvedPath().c_str());
            }
        }
    }
    cycles_shader_graph->add(imageTexture);
    return imageTexture;
}

ccl::ShaderNode*
matConvertUSDPreviewSurface(HdMaterialNode& usd_node,
                            ccl::ShaderGraph* cycles_shader_graph)
{
    ccl::PrincipledBsdfNode* principled = new ccl::PrincipledBsdfNode();
    principled->base_color              = ccl::make_float3(1.0f, 1.0f, 1.0f);

    // Convert params
    for (std::pair<TfToken, VtValue> params : usd_node.parameters) {
        if (params.first == _tokens->diffuseColor) {
            if (params.second.IsHolding<GfVec3f>()) {
                principled->base_color = vec3f_to_float3(
                    params.second.UncheckedGet<GfVec3f>());
            } else if (params.second.IsHolding<GfVec4f>()) {
                principled->base_color = vec4f_to_float3(
                    params.second.UncheckedGet<GfVec4f>());
            }

        } else if (params.first == _tokens->roughness) {
            if (params.second.IsHolding<float>()) {
                principled->roughness = params.second.UncheckedGet<float>();
            }

        } else if (params.first == _tokens->metallic) {
            if (params.second.IsHolding<float>()) {
                principled->metallic = params.second.UncheckedGet<float>();
            }

        } else if (params.first == _tokens->specular) {
            if (params.second.IsHolding<float>()) {
                principled->specular = params.second.UncheckedGet<float>();
            }
        }
    }

    cycles_shader_graph->add(principled);
    return principled;
}

TfToken
socketConverter(TfToken a_token)
{
    // TODO: Add check if preview surface
    if (a_token == _tokens->rgb || a_token == _tokens->r
        || a_token == _tokens->g || a_token == _tokens->b) {
        return _tokens->Color;
    } else if (a_token == _tokens->st) {
        return _tokens->Vector;
    } else if (a_token == _tokens->diffuseColor) {
        return _tokens->base_color;
    } else if (a_token == _tokens->result) {
        return _tokens->UV;
    }

    return a_token;
}

ccl::ShaderNode*
convertCyclesNode(HdMaterialNode& usd_node,
                  ccl::ShaderGraph* cycles_shader_graph)
{
    // Get Cycles node name
    std::string node_id = usd_node.identifier.GetString();

    bool has_valid_prefix = IsValidCyclesIdentifier(node_id);

    if (!has_valid_prefix) {
        // illegal node name
        TF_WARN("MATERIAL ERROR: Illegal cycles node name: %s",
                node_id.c_str());
        return nullptr;
    }

    ccl::ustring cycles_node_name = ccl::ustring(node_id.substr(7));

    // Find dynamic node type
    const ccl::NodeType* node_type = ccl::NodeType::find(cycles_node_name);
    if (!node_type) {
        TF_WARN("OMATERIAL ERRR: Could not find cycles node of type: %s",
                usd_node.identifier.GetString().c_str());
        return nullptr;
    }

    // Dynamic cycles node object
    ccl::ShaderNode* cyclesNode = (ccl::ShaderNode*)node_type->create(
        node_type);

    cycles_shader_graph->add(cyclesNode);

    // Convert cycles params
    for (std::pair<TfToken, VtValue> params : usd_node.parameters) {
        // Loop through all cycles inputs for matching usd shade param
        for (const ccl::SocketType& socket : cyclesNode->type->inputs) {
            // Early out if usd shade param doesn't match input name
            if (!ccl::string_iequals(params.first.GetText(),
                                     socket.name.string()))
                continue;

            // Ensure param has value
            if (params.second.IsEmpty()) {
                continue;
            }

            // Early out for invalid cycles types and flags
            if (socket.type == ccl::SocketType::CLOSURE
                || socket.type == ccl::SocketType::UNDEFINED)
                continue;
            if (socket.flags & ccl::SocketType::INTERNAL)
                continue;

            // TODO: Why do we do this?
            if (cycles_node_name == "normal_map")
                if (ccl::string_iequals("attribute", socket.name.string()))
                    continue;

            switch (socket.type) {
            case ccl::SocketType::INT: {
                cyclesNode->set(socket, params.second.Get<int>());
                break;
            }

            case ccl::SocketType::FLOAT: {
                cyclesNode->set(socket, params.second.Get<float>());
                break;
            }

            case ccl::SocketType::FLOAT_ARRAY: {
                if (params.second.IsHolding<VtFloatArray>()) {
                    ccl::array<float> val;
                    VtFloatArray floatArray = params.second.Get<VtFloatArray>();
                    val.resize(floatArray.size());
                    for (size_t i = 0; i < val.size(); i++) {
                        val[i] = floatArray[i];
                    }
                    cyclesNode->set(socket, val);
                }
                break;
            }

            case ccl::SocketType::ENUM: {
                if (params.second.IsHolding<int>()) {
                    cyclesNode->set(
                        socket, (*socket.enum_values)[params.second.Get<int>()]
                                    .string()
                                    .c_str());
                } else if (params.second.IsHolding<std::string>()) {
                    cyclesNode->set(socket,
                                    params.second.Get<std::string>().c_str());
                }
            } break;

            case ccl::SocketType::STRING: {
                std::string val;
                if (params.second.IsHolding<SdfAssetPath>()) {
// TODO:
// USD Issue-916 means that we cant resolve relative UDIM
// paths. This is fixed in 20.08. When we upgrade to that
// (And when houdini does). We can just use resolved path.
// For now, if the string has a UDIM in it, don't resolve.
// (This means relative UDIMs won't work)
#ifdef USD_HAS_UDIM_RESOLVE_FIX
                    val = std::string(params.second.Get<SdfAssetPath>()
                                          .GetResolvedPath()
                                          .c_str());
#else
                    std::string raw_path = std::string(
                        params.second.Get<SdfAssetPath>().GetAssetPath().c_str());
                    if (HdCyclesPathIsUDIM(raw_path)) {
                        val = raw_path;
                    } else {
                        val = std::string(params.second.Get<SdfAssetPath>()
                                              .GetResolvedPath()
                                              .c_str());
                    }
#endif
                } else if (params.second.IsHolding<TfToken>()) {
                    val = params.second.Get<TfToken>().GetString().c_str();
                    if (val.length() > 0)
                        val = pxr::TfMakeValidIdentifier(val);
                } else if (params.second.IsHolding<std::string>()) {
                    val = std::string(params.second.Get<std::string>().c_str());
                    if (val.length() > 0)
                        val = pxr::TfMakeValidIdentifier(val);
                }

                cyclesNode->set(socket, val.c_str());
            } break;

            case ccl::SocketType::COLOR:
            case ccl::SocketType::VECTOR:
            case ccl::SocketType::POINT:
            case ccl::SocketType::NORMAL: {
                if (params.second.IsHolding<GfVec4f>()) {
                    cyclesNode->set(socket, vec4f_to_float3(
                                                params.second.Get<GfVec4f>()));
                } else if (params.second.IsHolding<GfVec3f>()) {
                    cyclesNode->set(socket, vec3f_to_float3(
                                                params.second.Get<GfVec3f>()));
                }
            } break;

            case ccl::SocketType::COLOR_ARRAY:
            case ccl::SocketType::VECTOR_ARRAY:
            case ccl::SocketType::POINT_ARRAY:
            case ccl::SocketType::NORMAL_ARRAY: {
                if (params.second.IsHolding<VtVec4fArray>()) {
                    ccl::array<ccl::float3> val;
                    VtVec4fArray colarray = params.second.Get<VtVec4fArray>();
                    val.resize(colarray.size());
                    for (size_t i = 0; i < val.size(); i++) {
                        val[i] = vec4f_to_float3(colarray[i]);
                    }
                    cyclesNode->set(socket, val);
                } else if (params.second.IsHolding<VtVec3fArray>()) {
                    ccl::array<ccl::float3> val;
                    VtVec3fArray colarray = params.second.Get<VtVec3fArray>();
                    val.resize(colarray.size());
                    for (size_t i = 0; i < val.size(); i++) {
                        val[i] = vec3f_to_float3(colarray[i]);
                    }
                    cyclesNode->set(socket, val);
                }
            } break;
            }
        }
    }

    // TODO: Check proper type
    if (cycles_node_name == "image_texture") {
        ccl::ImageTextureNode* tex = (ccl::ImageTextureNode*)cyclesNode;

        // Handle udim tiles
        if (HdCyclesPathIsUDIM(tex->filename.string())) {
            HdCyclesParseUDIMS(tex->filename.string(), tex->tiles);
        }
    }

    return cyclesNode;
}

// TODO: This should be rewritten to better handle preview surface and cycles materials.
// Pretty sure it only works because the network map has the cycles material first in a list
static bool
GetMaterialNetwork(TfToken const& terminal, HdSceneDelegate* delegate,
                   HdMaterialNetworkMap const& networkMap,
                   HdCyclesRenderParam const& renderParam,
                   HdMaterialNetwork const** out_network,
                   ccl::ShaderGraph* graph)
{
    std::map<SdfPath, std::pair<HdMaterialNode*, ccl::ShaderNode*>> conversionMap;

    ccl::ShaderNode* output_node = nullptr;

    if (terminal == HdCyclesMaterialTerminalTokens->surface) {
        // Early out for already linked surface graph
        if (graph->output()->input("Surface")->link)
            return false;
    } else if (terminal == HdCyclesMaterialTerminalTokens->displacement) {
        // Early out for already linked displacement graph
        if (graph->output()->input("Displacement")->link)
            return false;
    } else if (terminal == HdCyclesMaterialTerminalTokens->volume) {
        // Early out for already linked volume graph
        if (graph->output()->input("Volume")->link) {
            return false;
        }
    }

    for (std::pair<TfToken, HdMaterialNetwork> net : networkMap.map) {
        if (net.first != terminal)
            continue;
        // Convert material nodes
        for (HdMaterialNode& node : net.second.nodes) {
            ccl::ShaderNode* cycles_node = nullptr;

            if (node.identifier == UsdImagingTokens->UsdPreviewSurface) {
                cycles_node = matConvertUSDPreviewSurface(node, graph);
            } else if (node.identifier == UsdImagingTokens->UsdUVTexture) {
                cycles_node = matConvertUSDUVTexture(node, graph);
            } else if (node.identifier
                       == UsdImagingTokens->UsdPrimvarReader_float2) {
                cycles_node = matConvertUSDPrimvarReader(node, graph);
            } else {
                cycles_node = convertCyclesNode(node, graph);
            }

            if (cycles_node != nullptr) {
                conversionMap.insert(
                    std::pair<SdfPath,
                              std::pair<HdMaterialNode*, ccl::ShaderNode*>>(
                        node.path, std::make_pair(&node, cycles_node)));
            }

            for (const pxr::SdfPath& tPath : networkMap.terminals) {
                if (node.path == tPath) {
                    output_node = cycles_node;

                    if (terminal == HdCyclesMaterialTerminalTokens->surface) {
                        if (cycles_node->output("BSDF") != NULL) {
                            graph->connect(cycles_node->output("BSDF"),
                                           graph->output()->input("Surface"));

                        } else if (cycles_node->output("Closure") != NULL) {
                            graph->connect(cycles_node->output("Closure"),
                                           graph->output()->input("Surface"));

                        } else if (cycles_node->output("Emission") != NULL) {
                            graph->connect(cycles_node->output("Emission"),
                                           graph->output()->input("Surface"));
                        }
                    }
                    if (terminal
                        == HdCyclesMaterialTerminalTokens->displacement) {
                        if (cycles_node->output("Displacement") != NULL) {
                            graph->connect(cycles_node->output("Displacement"),
                                           graph->output()->input(
                                               "Displacement"));
                        }
                    }
                    if (terminal == HdCyclesMaterialTerminalTokens->volume) {
                        if (cycles_node->output("Volume") != NULL) {
                            graph->connect(cycles_node->output("Volume"),
                                           graph->output()->input("Volume"));
                        }
                    }
                }
            }
        }

        // Link material nodes
        for (const HdMaterialRelationship& matRel : net.second.relationships) {
            ccl::ShaderNode* tonode
                = (ccl::ShaderNode*)conversionMap[matRel.outputId].second;
            ccl::ShaderNode* fromnode
                = (ccl::ShaderNode*)conversionMap[matRel.inputId].second;

            HdMaterialNode* hd_tonode   = conversionMap[matRel.outputId].first;
            HdMaterialNode* hd_fromnode = conversionMap[matRel.inputId].first;

            std::string to_identifier   = hd_tonode->identifier.GetString();
            std::string from_identifier = hd_fromnode->identifier.GetString();

            ccl::ShaderOutput* output = NULL;
            ccl::ShaderInput* input   = NULL;

            bool to_has_valid_prefix   = IsValidCyclesIdentifier(to_identifier);
            bool from_has_valid_prefix = IsValidCyclesIdentifier(
                from_identifier);

            // Converts Preview surface connections
            // TODO: Handle this check better
            TfToken cInputName = matRel.inputName;
            if (!from_has_valid_prefix)
                cInputName = socketConverter(cInputName);
            TfToken cOutputName = matRel.outputName;
            if (!to_has_valid_prefix)
                cOutputName = socketConverter(cOutputName);

            if (tonode == nullptr) {
                TF_WARN("MATERIAL ERROR: Could not link, tonode was null: %s",
                        matRel.outputId.GetString().c_str());
                continue;
            } else if (fromnode == nullptr) {
                TF_WARN("MATERIAL ERROR: Could not link, fromnode was null: %s",
                        matRel.inputId.GetString().c_str());
                continue;
            }

            if (fromnode) {
                for (ccl::ShaderOutput* out : fromnode->outputs) {
                    if (!out)
                        continue;

                    if (ccl::string_iequals(out->socket_type.name.string(),
                                            cInputName)) {
                        output = out;
                        break;
                    }
                }
            }

            if (tonode) {
                for (ccl::ShaderInput* in : tonode->inputs) {
                    if (!in)
                        continue;
                    if (ccl::string_iequals(in->socket_type.name.string(),
                                            cOutputName)) {
                        input = in;
                        break;
                    }
                }
            }

            if (output && input) {
                if (input->link) {
                    continue;
                }
                graph->connect(output, input);
            }
        }

        // TODO: This is to allow retroactive material_output node support
        // As this becomes phased out, we can remove this.
        if (output_node != nullptr) {
            if (graph->output()->input("Surface")->link == nullptr) {
                if (output_node->input("Surface") != NULL) {
                    if (output_node->name == "output") {
                        if (output_node->input("Surface")->link) {
                            if (terminal
                                == HdCyclesMaterialTerminalTokens->surface) {
                                graph->connect(
                                    output_node->input("Surface")->link,
                                    graph->output()->input("Surface"));
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

void
HdCyclesMaterial::Sync(HdSceneDelegate* sceneDelegate,
                       HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    auto cyclesRenderParam     = static_cast<HdCyclesRenderParam*>(renderParam);
    HdCyclesRenderParam* param = (HdCyclesRenderParam*)renderParam;

    const SdfPath& id = GetId();

    param->GetCyclesScene()->mutex.lock();
    bool material_updated = false;

    HdDirtyBits bits = *dirtyBits;


    if (*dirtyBits & HdMaterial::DirtyResource) {
        VtValue vtMat = sceneDelegate->GetMaterialResource(id);

        if (vtMat.IsHolding<HdMaterialNetworkMap>()) {
            if (m_shaderGraph) {
                m_shaderGraph = new ccl::ShaderGraph();
            }

            auto& networkMap = vtMat.UncheckedGet<HdMaterialNetworkMap>();

            HdMaterialNetwork const* surface      = nullptr;
            HdMaterialNetwork const* displacement = nullptr;
            HdMaterialNetwork const* volume       = nullptr;

            if (GetMaterialNetwork(HdCyclesMaterialTerminalTokens->surface,
                                   sceneDelegate, networkMap,
                                   *cyclesRenderParam, &surface,
                                   m_shaderGraph)) {
                if (m_shader && m_shaderGraph) {
                    material_updated = true;
                }
            }

            if (GetMaterialNetwork(HdCyclesMaterialTerminalTokens->displacement,
                                   sceneDelegate, networkMap,
                                   *cyclesRenderParam, &displacement,
                                   m_shaderGraph)) {
                if (m_shader && m_shaderGraph) {
                    material_updated = true;
                }
            }

            if (GetMaterialNetwork(HdCyclesMaterialTerminalTokens->volume,
                                   sceneDelegate, networkMap,
                                   *cyclesRenderParam, &volume,
                                   m_shaderGraph)) {
                if (m_shader && m_shaderGraph) {
                    material_updated = true;
                }
            }

            if (material_updated) {
                m_shader->graph = m_shaderGraph;
            } else {
                TF_CODING_WARNING("Material type not supported");
            }
        }
    }

    if (*dirtyBits & HdMaterial::DirtyResource) {
#ifdef USE_USD_CYCLES_SCHEMA

        TfToken displacementMethod = _HdCyclesGetParam<TfToken>(
            sceneDelegate, id,
            usdCyclesTokens->cyclesMaterialDisplacement_method,
            usdCyclesTokens->displacement_bump);

        if (m_shader->displacement_method
            != DISPLACEMENT_CONVERSION[displacementMethod]) {
            m_shader->displacement_method
                = DISPLACEMENT_CONVERSION[displacementMethod];
        }

        m_shader->pass_id
            = _HdCyclesGetParam<int>(sceneDelegate, id,
                                     usdCyclesTokens->cyclesMaterialPass_id,
                                     m_shader->pass_id);

        m_shader->use_mis
            = _HdCyclesGetParam<bool>(sceneDelegate, id,
                                      usdCyclesTokens->cyclesMaterialUse_mis,
                                      m_shader->use_mis);

        m_shader->use_transparent_shadow = _HdCyclesGetParam<bool>(
            sceneDelegate, id,
            usdCyclesTokens->cyclesMaterialUse_transparent_shadow,
            m_shader->use_transparent_shadow);

        m_shader->heterogeneous_volume = _HdCyclesGetParam<bool>(
            sceneDelegate, id,
            usdCyclesTokens->cyclesMaterialHeterogeneous_volume,
            m_shader->heterogeneous_volume);

        m_shader->volume_step_rate = _HdCyclesGetParam<float>(
            sceneDelegate, id, usdCyclesTokens->cyclesMaterialVolume_step_rate,
            m_shader->volume_step_rate);

        TfToken volume_interpolation = _HdCyclesGetParam<TfToken>(
            sceneDelegate, id,
            usdCyclesTokens->cyclesMaterialVolume_interpolation_method,
            usdCyclesTokens->volume_interpolation_linear);

        if (m_shader->volume_interpolation_method
            != VOLUME_INTERPOLATION_CONVERSION[volume_interpolation]) {
            m_shader->volume_interpolation_method
                = VOLUME_INTERPOLATION_CONVERSION[volume_interpolation];
        }

        TfToken volume_sampling = _HdCyclesGetParam<TfToken>(
            sceneDelegate, id,
            usdCyclesTokens->cyclesMaterialVolume_sampling_method,
            usdCyclesTokens->volume_sampling_multiple_importance);

        if (m_shader->volume_sampling_method
            != VOLUME_SAMPLING_CONVERSION[volume_sampling]) {
            m_shader->volume_sampling_method
                = VOLUME_SAMPLING_CONVERSION[volume_sampling];
        }
        material_updated = true;

#endif
    }

    if (material_updated) {
        if (m_shader->graph != m_shaderGraph)
            m_shader->set_graph(m_shaderGraph);

        m_shader->tag_update(param->GetCyclesScene());
        m_shader->tag_used(param->GetCyclesScene());
        param->Interrupt();
    }

    param->GetCyclesScene()->mutex.unlock();

    *dirtyBits = Clean;
}

ccl::Shader*
HdCyclesMaterial::GetCyclesShader() const
{
    return m_shader;
}

HdDirtyBits
HdCyclesMaterial::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllDirty;
}

void
HdCyclesMaterial::Reload()
{
}

bool
HdCyclesMaterial::IsValid() const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE